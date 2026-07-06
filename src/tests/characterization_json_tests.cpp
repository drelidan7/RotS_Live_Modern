#include "../character_json.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

const char* const kGoldenPath = "goldens/character_seed_fixture.json";

std::string read_file(const char* path)
{
    std::ifstream in(path);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// A fixture exercising every field family: strings (incl. characters needing
// JSON escaping), flags, professions, abilities, points. Extend it whenever a
// new field family is added to CharacterData.
character_json::CharacterData make_fixture()
{
    character_json::CharacterData character;
    // Intentionally the real schema epoch, not a distinct fake value:
    // deserialize_character_from_json rejects any schema_version !=
    // CHARACTER_JSON_SCHEMA_VERSION ("Unsupported character schema version.",
    // character_json.cpp), so a fake value would break GoldenRoundTripsByteStable.
    // Set explicitly (rather than riding the struct default) so the fixture
    // pins the version the golden was generated against.
    character.schema_version = character_json::CHARACTER_JSON_SCHEMA_VERSION;
    character.character_name = "Goldenfix";
    character.title = "the \"Characterization\" Fixture";
    character.description = "Line one.\nLine two with a tab:\t.";

    character.idnum = 424242;
    character.race = 3;
    character.sex = 1;
    character.bodytype = 2;
    character.language = 4;
    character.hometown = 1200;
    character.weight = 205;
    character.height = 71;
    character.level = 42;
    character.alignment = -350;
    character.load_room = 3054;
    character.spells_to_learn = 7;
    character.wimp_level = 15;
    character.freeze_level = 6;
    character.raw_perception = 82;
    character.perception = 88;
    character.mini_level = 3;
    character.max_mini_level = 5;
    character.morale = 61;
    character.owner = 777;
    character.rerolls = 4;
    character.leg_encumbrance = 9;
    character.rp_flag = 21;
    character.will_teach = 555555;
    character.tactics = TACTICS_BERSERK;
    character.shooting = SHOOTING_FAST;
    character.casting = CASTING_FAST;
    character.two_handed = true;

    character.mage.level = 5;
    character.mage.points = 20;
    character.mage.coeff = 20;
    character.mage.experience = 10000;

    character.mystic.level = 8;
    character.mystic.points = 30;
    character.mystic.coeff = 30;
    character.mystic.experience = 20000;

    character.ranger.level = 12;
    character.ranger.points = 40;
    character.ranger.coeff = 40;
    character.ranger.experience = 30000;

    character.warrior.level = 42;
    character.warrior.points = 99;
    character.warrior.coeff = 99;
    character.warrior.experience = 99999;

    character.temporary_abilities.str = 15;
    character.temporary_abilities.lea = 9;
    character.temporary_abilities.intel = 10;
    character.temporary_abilities.wil = 11;
    character.temporary_abilities.dex = 14;
    character.temporary_abilities.con = 13;
    character.temporary_abilities.hit = 350;
    character.temporary_abilities.mana = 70;
    character.temporary_abilities.move = 95;

    character.rolled_abilities.str = 16;
    character.rolled_abilities.lea = 8;
    character.rolled_abilities.intel = 9;
    character.rolled_abilities.wil = 10;
    character.rolled_abilities.dex = 13;
    character.rolled_abilities.con = 12;
    character.rolled_abilities.hit = 300;
    character.rolled_abilities.mana = 60;
    character.rolled_abilities.move = 85;

    character.points.bodypart_hit = { 100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50 };
    character.points.gold = 5000;
    character.points.experience = 987654;
    character.points.spirit = 45;
    character.points.mana_regen = 11;
    character.points.health_regen = 9;
    character.points.move_regen = 8;
    character.points.ob = 110;
    character.points.damage = 18;
    character.points.energy_regen = 14;
    character.points.parry = 38;
    character.points.dodge = 31;
    character.points.encumbrance = 22;
    character.points.willpower = 50;
    character.points.spell_pen = 6;
    character.points.spell_power = 9;

    character.conditions.drunk = 2;
    character.conditions.full = 18;
    character.conditions.thirst = 21;

    character.timers.birth = 1700000001;
    character.timers.last_logon = 1720000000;
    character.timers.played_seconds = 54321;
    character.timers.retired_on = 1706000000;

    character.color_mask = 0xABCDEF;

    // colors/color_settings must each hold exactly MAX_COLOR_FIELDS entries
    // (validated by validate_color_array_range / validate_color_settings).
    // Most slots get a plain ansi16 selection via `colors`; two slots
    // (magic/weather, indices 13/14) also carry an explicit truecolor
    // ColorSettingData so both serialization paths are exercised.
    character.colors.assign(MAX_COLOR_FIELDS, 0);
    character.color_settings.assign(MAX_COLOR_FIELDS, character_json::ColorSettingData {});
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index)
        character.colors[index] = (index % (CBWHT - CNRM)) + 1;

    character.color_settings[13].foreground.mode = COLOR_VALUE_TRUECOLOR;
    character.color_settings[13].foreground.value = CBMAG;
    character.color_settings[13].foreground.red = 180;
    character.color_settings[13].foreground.green = 80;
    character.color_settings[13].foreground.blue = 255;

    character.color_settings[14].background.mode = COLOR_VALUE_TRUECOLOR;
    character.color_settings[14].background.value = CBBLU;
    character.color_settings[14].background.red = 10;
    character.color_settings[14].background.green = 20;
    character.color_settings[14].background.blue = 35;

    character.talks = { 50, 75, 100 };

    character.skills.assign(MAX_SKILLS, 0);
    character.skills[0] = 12;
    character.skills[1] = 34;
    character.skills[2] = 56;
    character.skills[3] = 78;
    character.skills[4] = 90;

    character.player_flags = { "writing", "frozen" };
    character.preference_flags = { "brief", "color" };
    character.affected_flags = { "sanctuary", "invisible" };
    character.hide_flags = { "hiding_well", "snuck_in" };

    character_json::AffectData affect_one;
    affect_one.type = 101;
    affect_one.duration = 6;
    affect_one.time_phase = 1;
    affect_one.modifier = 3;
    affect_one.location = APPLY_OB;
    affect_one.bitvector = 999; // Not serialized (see report); set anyway per the "every member" rule.
    affect_one.counter = 7;
    affect_one.flags = { "detect_magic", "curse" };

    character_json::AffectData affect_two;
    affect_two.type = 202;
    affect_two.duration = 12;
    affect_two.time_phase = 2;
    affect_two.modifier = 5;
    affect_two.location = APPLY_WILL;
    affect_two.bitvector = 1000; // Not serialized (see report); set anyway per the "every member" rule.
    affect_two.counter = 3;
    affect_two.flags = { "poison" };

    character.affects = { affect_one, affect_two };

    return character;
}

} // namespace

TEST(CharacterizationJson, SerializeMatchesGolden)
{
    std::string json = character_json::serialize_character_to_json(make_fixture());

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        std::ofstream out(kGoldenPath);
        out << json;
        SUCCEED() << "golden updated";
        return;
    }

    EXPECT_EQ(read_file(kGoldenPath), json)
        << "Character JSON format drifted. If intentional (schema change), bump "
           "CHARACTER_JSON_SCHEMA_VERSION, rerun with UPDATE_GOLDENS=1, commit.";
}

TEST(CharacterizationJson, GoldenRoundTripsByteStable)
{
    std::string golden = read_file(kGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run SerializeMatchesGolden with UPDATE_GOLDENS=1 first";

    character_json::CharacterData parsed;
    std::string error;
    ASSERT_TRUE(character_json::deserialize_character_from_json(golden, &parsed, &error)) << error;
    EXPECT_EQ(golden, character_json::serialize_character_to_json(parsed));
}
