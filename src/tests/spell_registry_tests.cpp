// Task 1 (entity-seed plan): assign_spell_pointers() (spell_pa.cpp)
// re-populates the 69 spell_* function-pointer cells that consts.cpp's
// skills[] initializer used to embed directly -- a hard-coded upward
// reference into mystic.cpp/mage.cpp that blocked consts.cpp from joining
// the L1 rots_core library (spec Sec 3 caveats; see CMakeLists.txt's
// ROTS_CORE_SOURCES comment). kExpectedSpells below is a positional
// {index, name, function} transcription of consts.cpp:382-634 taken BEFORE
// that table was nulled out -- row N of the old initializer IS skill index
// N (cross-checked against spells.h: SPELL_DETECT_HIDDEN=41,
// SPELL_MAGIC_MISSILE=71, SPELL_CONFUSE=111, SPELL_EXPOSE_ELEMENTS=112,
// SPELL_MASS_INSIGHT=160). Asserting both the pointer AND the name column
// (independent data, not derived from the same extraction pass) catches an
// off-by-one that a pointer-only check would not.
#include "../spells.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

// Declared in spells.h; forward-declared here too only to make this test's
// dependency on it explicit at the point of use.
void assign_spell_pointers();

// consts.cpp's global skill table -- not declared in any header (matching
// every other .cpp that references it, e.g. spell_pa.cpp:38); this is the
// array assign_spell_pointers() populates and this suite asserts against.
extern struct skill_data skills[MAX_SKILLS];

namespace {

// The exact skill_data::spell_pointer signature (spells.h's ASPELL macro).
using SpellFn = void (*)(char_data*, char*, int, char_data*, obj_data*, int, int);

struct ExpectedSpell {
    int index; // skills[] index this spell/skill occupies (positional, from the pre-null consts.cpp).
    const char* name; // skills[index].name expected after assign_spell_pointers() runs.
    SpellFn fn; // skills[index].spell_pointer expected after assign_spell_pointers() runs.
};

// clang-format off
constexpr ExpectedSpell kExpectedSpells[] = {
    { 41, "detect hidden", spell_detect_hidden },
    { 42, "evasion", spell_evasion },
    { 43, "poison", spell_poison },
    { 44, "resist poison", spell_resist_poison },
    { 45, "curing saturation", spell_curing },
    { 46, "restlessness", spell_restlessness },
    { 47, "resist magic", spell_resist_magic },
    { 48, "slow digestion", spell_slow_digestion },
    { 49, "dispel regeneration", spell_dispel_regeneration },
    { 50, "insight", spell_insight },
    { 51, "pragmatism", spell_pragmatism },
    { 52, "haze", spell_haze },
    { 53, "fear", spell_fear },
    { 54, "divination", spell_divination },
    { 56, "sanctuary", spell_sanctuary },
    { 57, "vitality", spell_vitality },
    { 58, "terror", spell_terror },
    { 60, "enchant weapon", spell_enchant_weapon },
    { 62, "summon", spell_summon },
    { 63, "hallucinate", spell_hallucinate },
    { 64, "regeneration", spell_regeneration },
    { 65, "guardian", spell_guardian },
    { 66, "infravision", spell_infravision },
    { 67, "curse", spell_curse },
    { 68, "revive", spell_revive },
    { 69, "detect magic", spell_detect_magic },
    { 70, "shift", spell_shift },
    { 71, "magic missile", spell_magic_missile },
    { 72, "reveal life", spell_reveal_life },
    { 73, "locate living", spell_locate_living },
    { 74, "cure self", spell_cure_self },
    { 75, "chill ray", spell_chill_ray },
    { 76, "blink", spell_blink },
    { 77, "freeze", spell_freeze },
    { 78, "lightning bolt", spell_lightning_bolt },
    { 79, "vitalize self", spell_vitalize_self },
    { 80, "flash", spell_flash },
    { 81, "earthquake", spell_earthquake },
    { 82, "create light", spell_create_light },
    { 83, "death ward", spell_death_ward },
    { 84, "dark bolt", spell_dark_bolt },
    { 85, "mist of baazunga", spell_mist_of_baazunga },
    { 86, "mind block", spell_mind_block },
    { 87, "remove poison", spell_remove_poison },
    { 88, "beacon", spell_beacon },
    { 89, "protection", spell_protection },
    { 90, "blaze", spell_blaze },
    { 91, "firebolt", spell_firebolt },
    { 92, "relocate", spell_relocate },
    { 93, "cone of cold", spell_cone_of_cold },
    { 94, "identify", spell_identify },
    { 96, "fireball", spell_fireball },
    { 98, "searing darkness", spell_searing_darkness },
    { 99, "lightning strike", spell_lightning_strike },
    { 100, "word of pain", spell_word_of_pain },
    { 101, "word of sight", spell_word_of_sight },
    { 102, "word of agony", spell_word_of_agony },
    { 103, "shout of pain", spell_shout_of_pain },
    { 104, "word of shock", spell_word_of_shock },
    { 105, "spear of darkness", spell_spear_of_darkness },
    { 106, "leach", spell_leach },
    { 107, "black arrow", spell_black_arrow },
    { 108, "shield", spell_shield },
    { 109, "detect evil", spell_detect_evil },
    { 111, "confuse", spell_confuse },
    { 112, "expose elements", spell_expose_elements },
    { 158, "mass regeneration", spell_mass_regeneration },
    { 159, "mass vitality", spell_mass_vitality },
    { 160, "mass insight", spell_mass_insight },
};
// clang-format on

bool is_expected_index(int index)
{
    return std::any_of(std::begin(kExpectedSpells), std::end(kExpectedSpells),
        [index](const ExpectedSpell& expected) { return expected.index == index; });
}

} // namespace

TEST(SpellRegistry, AssignSpellPointersSetsExpectedPointersAndNames)
{
    assign_spell_pointers();

    for (const ExpectedSpell& expected : kExpectedSpells) {
        EXPECT_EQ(skills[expected.index].spell_pointer, expected.fn)
            << "index " << expected.index << " (" << expected.name << ")";
        EXPECT_STREQ(skills[expected.index].name, expected.name) << "index " << expected.index;
    }
}

TEST(SpellRegistry, AssignSpellPointersLeavesOtherIndicesNull)
{
    assign_spell_pointers();

    for (int index = 0; index < MAX_SKILLS; ++index) {
        if (!is_expected_index(index)) {
            EXPECT_EQ(skills[index].spell_pointer, nullptr) << "index " << index;
        }
    }
}
