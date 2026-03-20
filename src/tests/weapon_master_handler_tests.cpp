#include "../warrior_spec_handlers.h"
#include "ObjFlagDataBuilder.h"
#include <gtest/gtest.h>

namespace {

struct WeaponMasterTestContext {
    char_data character{};
    char_prof_data profs{};
    obj_data weapon{};

    WeaponMasterTestContext(game_types::player_specs specialization, game_types::weapon_type weapon_type) {
        character.profs = &profs;
        profs.specialization = static_cast<int>(specialization);

        weapon.obj_flags = builders::ObjFlagDataBuilder().setWeaponType(weapon_type).build();
        weapon.obj_flags.type_flag = ITEM_WEAPON;
    }
};

} // namespace

TEST(WeaponMasterHandler, ReturnsDefaultAttackSpeedMultiplierForNonSpecialists) {
    WeaponMasterTestContext context(game_types::PS_None, game_types::WT_PIERCING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.0f)
        << "Expected non-weapon masters to receive no attack speed bonus.";
}

TEST(WeaponMasterHandler, GrantsAttackSpeedBonusForPiercingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_PIERCING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.15f)
        << "Expected weapon masters using piercing weapons to gain a speed bonus.";
}

TEST(WeaponMasterHandler, ReadsWeaponTypeFromWieldedEquipmentInDefaultConstructor) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_PIERCING);
    context.character.equipment[WIELD] = &context.weapon;
    player_spec::weapon_master_handler handler(&context.character);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.15f)
        << "Expected the default constructor to read the weapon type from the wielded weapon slot.";
}

TEST(WeaponMasterHandler, GrantsBonusDamageForCleavingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_CLEAVING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_total_damage(100), 115)
        << "Expected cleaving weapons to gain 15% bonus damage for weapon masters.";
}

TEST(WeaponMasterHandler, LeavesDamageUnchangedForNonBonusWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_total_damage(100), 100)
        << "Expected slashing weapons to keep their original damage total.";
}

TEST(WeaponMasterHandler, GrantsOffensiveBonusForBludgeoningWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_BLUDGEONING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_OB(), 10)
        << "Expected bludgeoning weapon masters to gain a +10 offensive bonus.";
}

TEST(WeaponMasterHandler, GrantsBalancedBonusesForSlashingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_OB(), 5)
        << "Expected slashing weapon masters to gain a +5 offensive bonus.";
    EXPECT_EQ(handler.get_bonus_PB(), 5)
        << "Expected slashing weapon masters to gain a +5 parry bonus.";
}

TEST(WeaponMasterHandler, GrantsParryBonusForStabbingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_STABBING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_PB(), 10)
        << "Expected stabbing weapon masters to gain a +10 parry bonus.";
}

TEST(WeaponMasterHandler, ReturnsNoBonusesForNonSpecialists) {
    WeaponMasterTestContext context(game_types::PS_None, game_types::WT_BLUDGEONING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_OB(), 0)
        << "Expected non-weapon masters to receive no offensive bonus.";
    EXPECT_EQ(handler.get_bonus_PB(), 0)
        << "Expected non-weapon masters to receive no parry bonus.";
}

TEST(WeaponMasterHandler, AppendsReadableScoreMessageForSlashingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);
    char message_buffer[256] = {};

    const int written = handler.append_score_message(message_buffer);

    EXPECT_GT(written, 0)
        << "Expected weapon masters to receive a score message for supported weapon types.";
    EXPECT_STREQ(message_buffer, "Your mastery grants balanced prowess and occasional swift strikes.\r\n")
        << "Expected the slashing weapon score message to match the in-game description.";
}

TEST(WeaponMasterHandler, ReturnsNoScoreMessageForNonSpecialists) {
    WeaponMasterTestContext context(game_types::PS_None, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);
    char message_buffer[256] = {};

    EXPECT_EQ(handler.append_score_message(message_buffer), 0)
        << "Expected non-weapon masters to receive no specialization score message.";
}
