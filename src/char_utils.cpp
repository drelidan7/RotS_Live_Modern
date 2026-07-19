
#include "char_utils.h"
#include "environment_utils.h"
#include "object_utils.h"
#include "spells.h"
#include "text_view.h"

#include "db.h" // for get_encumb_table
#include "handler.h" // fname/other_side/other_side_num declared here, defined below

#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/tables.h" // for pc_race_keywords -- keyword_matches_char() (placement-seam Task 4)
#include "rots/core/types.h"
#include "entity_hooks.h"
#include "utils.h"
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <ctype.h> // for isalpha, used by the relocated fname()
#include <format>
#include <iterator>

#include "comm.h" // for send_to_char
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

struct race_bodypart_data;

// TODO(dgurley):  Move these tables elsewhere or provide accessors or something.
extern sh_int square_root[];
// extern race_bodypart_data bodyparts[MAX_BODYTYPES]; // Due to where this is located, this currently isn't possible to support here.

//============================================================================
// fname()/other_side()/other_side_num() relocated verbatim from handler.cpp
// (entity-completion Task 1): fname() is pure text manipulation over its own
// private scratch buffer; other_side()/other_side_num() are pure
// IS_NPC/AFF_CHARM/GET_RACE/RACE_* macro logic -- neither touches
// world/live-state. Declarations stay in handler.h.
char fname_nameholder[100];
char* fname(char* namelist)
{
    //   char	holder[30];
    char* point;

    for (point = fname_nameholder; isalpha(*namelist); namelist++, point++)
        *point = *namelist;

    *point = '\0';

    return (fname_nameholder);
}

/*
 * Decide if `character' and `other' are on the same side of the race
 * war.  Return 0 if they are, return 1 if they aren't.
 */
int other_side(const char_data* character, const char_data* other)
{
    if (IS_NPC(other) && !IS_AFFECTED(other, AFF_CHARM))
        return 0;
    if (IS_NPC(character) && !IS_AFFECTED(character, AFF_CHARM))
        return 0;
    if ((GET_RACE(character) == RACE_GOD) || (GET_RACE(other) == RACE_GOD))
        return 0;
    if (RACE_EAST(other) && !(RACE_EAST(character)))
        return 1;
    if (!(RACE_EAST(other)) && RACE_EAST(character))
        return 1;
    if (RACE_MAGI(other) && !(RACE_MAGI(character)))
        return 1;
    if (!(RACE_MAGI(other)) && RACE_MAGI(character))
        return 1;
    if (RACE_EVIL(other) && RACE_GOOD(character))
        return 1;
    if (RACE_GOOD(other) && RACE_EVIL(character))
        return 1;

    return 0;
}

int other_side_num(int ch_race, int i_race)
{
    if ((ch_race == RACE_GOD) || (i_race == RACE_GOD))
        return 0;
    if ((ch_race <= RACE_BEORNING) && (i_race <= RACE_BEORNING))
        return 0;

    if ((ch_race >= RACE_URUK) && (ch_race != RACE_MAGUS) && (ch_race != RACE_EASTERLING) && (ch_race != RACE_HARADRIM) && (i_race >= RACE_URUK) && (i_race != RACE_MAGUS) && (i_race != RACE_EASTERLING) && (i_race != RACE_HARADRIM))
        return 0;

    if (((ch_race == RACE_MAGUS) || (ch_race == RACE_HARADRIM)) && ((i_race == RACE_MAGUS) || (i_race == RACE_HARADRIM)))
        return 0;

    if (ch_race == i_race)
        return 0;

    return 1;
}
//============================================================================

namespace utils {
//============================================================================
bool is_pc(const char_data& character)
{
    return !is_npc(character);
}

// utils::is_npc() relocated to entity_lifecycle.cpp (entity-seed Task 6,
// controller-adjudicated relocation); declaration unchanged (char_utils.h).

//============================================================================
bool is_mob(const char_data& character)
{
    return is_npc(character) && character.nr >= 0;
}

//============================================================================
bool is_retired(const char_data& character)
{
    return utils::is_set(character.specials2.act, (long)PLR_RETIRED);
}

//============================================================================
bool is_mob_flagged(const char_data& mob, long flag)
{
    return is_npc(mob) && utils::is_set(mob.specials2.act, flag);
}

//============================================================================
bool is_player_flagged(const char_data& character, long flag)
{
    return !is_npc(character) && utils::is_set(character.specials2.act, flag);
}

//============================================================================
bool is_preference_flagged(const char_data& character, long flag)
{
    return utils::is_set(character.specials2.pref, flag);
}

//============================================================================
// Apparently this isn't used.
//============================================================================
bool is_player_mode_on(const char_data&, long)
{
    return false;
    // there is no character.specials2.mode
    // return base_utils::is_set(character.specials2.mode, flag);
}

//============================================================================
bool is_affected_by(const char_data& character, long skill_id)
{
    return utils::is_set(character.specials.affected_by, skill_id);
}

bool is_fighting(const char_data& character)
{
    return character.specials.fighting;
}

void add_spirits(char_data* character, int spirits)
{
    if ((character->points.spirit + spirits) <= MAX_SPIRITS) {
        character->points.spirit += spirits;
    }
}

void set_spirits(char_data* character, int spirits)
{
    if (spirits <= MAX_SPIRITS) {
        character->points.spirit = spirits;
    }
}

int get_spirits(char_data* character)
{
    return character->points.spirit;
}

int get_current_moves(char_data* character)
{
    return character->tmpabilities.move;
}

int get_max_moves(char_data* character)
{
    return character->abilities.move;
}

int get_current_hit(const char_data& character)
{
    return character.tmpabilities.hit;
}

int get_max_hit(const char_data& character)
{
    return character.abilities.hit;
}

//============================================================================
affected_type* is_affected_by_spell(char_data& character, int skill_id)
{
    int count = 0;
    for (affected_type* affect = character.affected; affect && count < MAX_AFFECT; affect = affect->next) {
        if (affect->type == skill_id) {
            return affect;
        }
        ++count;
    }

    return nullptr;
}

//============================================================================
const char* his_or_her(const char_data& character)
{
    switch (character.player.sex) {
    case 0:
        return "its";
        break;
    case 1:
        return "his";
        break;
    case 2:
        return "her";
        break;
    default:
        return "its";
        break;
    }
}

//============================================================================
const char* he_or_she(const char_data& character)
{
    switch (character.player.sex) {
    case 0:
        return "it";
        break;
    case 1:
        return "he";
        break;
    case 2:
        return "she";
        break;
    default:
        return "it";
        break;
    }
}

//============================================================================
const char* him_or_her(const char_data& character)
{
    switch (character.player.sex) {
    case 0:
        return "it";
        break;
    case 1:
        return "him";
        break;
    case 2:
        return "her";
        break;
    default:
        return "it";
        break;
    }
}

//============================================================================
int get_tactics(const char_data& character)
{
    if (is_npc(character))
        return 0;

    return character.specials.tactics;
}

// set_tactics() relocated to entity_lifecycle.cpp (persist-split PS Task 4,
// controller-adjudicated relocation); declaration unchanged (char_utils.h).

//============================================================================
int get_shooting(const char_data& character)
{
    if (is_npc(character))
        return 0;

    return character.specials.shooting;
}

// set_shooting() relocated to entity_lifecycle.cpp (persist-split PS Task 4,
// controller-adjudicated relocation); declaration unchanged (char_utils.h).

//============================================================================
int get_casting(const char_data& character)
{
    if (is_npc(character))
        return 0;

    return character.specials.casting;
}

// set_casting() relocated to entity_lifecycle.cpp (persist-split PS Task 4,
// controller-adjudicated relocation) with one mechanical substitution --
// the relocated body uses !is_npc(character) instead of is_pc(character),
// since is_pc() itself stays here (app layer) and calling it from
// rots_entity would be an upward edge; see entity_lifecycle.cpp's copy for
// why that substitution is provably identical. Declaration unchanged
// (char_utils.h).

//============================================================================
int get_condition(const char_data& character, int index)
{
    if (index > 2 || index < 0)
        return 0;

    return character.specials2.conditions[index];
}

//============================================================================
void set_condition(char_data& character, int index, sh_int value)
{
    if (index >= 0 && index <= 2) {
        character.specials2.conditions[index] = value;
    }
}

//============================================================================
int get_index(const char_data& character)
{
    if (is_npc(character))
        return -1;

    return character.player_index;
}

// utils::get_name() relocated to entity_lifecycle.cpp (entity-seed Task 6,
// controller-adjudicated relocation, third pass); declaration unchanged
// (char_utils.h).

//============================================================================
const char* get_skill_name(const int skill_id)
{
    const skill_data* skills = get_skill_array();
    const char* skill_name = skills[skill_id].name;
    return skill_name;
}

//============================================================================
int get_level_a(const char_data& character)
{
    if (is_npc(character))
        return character.player.level;

    return std::min(character.player.level, LEVEL_MAX);
}

//============================================================================
int get_level_legend_cap(const char_data& character)
{
    return get_level_a(character);
}

//============================================================================
int get_level_b(const char_data& character)
{
    if (is_npc(character))
        return character.player.level;

    return std::min(character.player.level, character.player.level / 3 + (LEVEL_MAX * 2 / 3));
}

//============================================================================
int get_prof_level(int prof, const char_data& character)
{
    if (prof == PROF_GENERAL || is_npc(character) || prof > MAX_PROFS)
        return character.player.level;

    return character.profs->prof_level[prof];
}

//============================================================================
int get_max_race_prof_level(int prof, const char_data& character)
{
    int max_prof_level = 30;

    if (character.player.race == RACE_ORC) {
        max_prof_level = 20;
    } else if (character.player.race == RACE_URUK && prof == PROF_MAGE) {
        max_prof_level = 27;
    }

    return max_prof_level;
}

//============================================================================
void set_prof_level(int prof, char_data& character, sh_int value)
{
    if (prof == PROF_GENERAL || prof > MAX_PROFS || is_npc(character))
        return;

    character.profs->prof_level[prof] = value;
}

//============================================================================
int get_prof_coof(int prof, const char_data& character)
{
    if (prof > MAX_PROFS)
        return 0; // should add assert functionality for this

    if (prof == PROF_GENERAL)
        return 1000;

    int prof_coof = character.profs->prof_coof[prof];
    // TODO(dgurley):  square_root is an externed array; fix this crap.
    int return_prof_coof = square_root[prof_coof];

    if (character.player.race == RACE_ORC) {
        return_prof_coof = (return_prof_coof * 2 + 2) / 3;
    } else if (character.player.race == RACE_URUK && prof == PROF_MAGE) {
        return_prof_coof -= 100;
    }

    return return_prof_coof;
}

// utils::get_prof_points() relocated to entity_lifecycle.cpp (entity-seed
// Task 6, controller-adjudicated relocation); declaration unchanged
// (char_utils.h).

//============================================================================
int get_highest_coeffs(const char_data& character)
{
    int coeffs[4];
    int prof = 0;
    coeffs[0] = get_prof_level(PROF_MAGE, character);
    coeffs[1] = get_prof_level(PROF_CLERIC, character);
    coeffs[2] = get_prof_level(PROF_RANGER, character);
    coeffs[3] = get_prof_level(PROF_WARRIOR, character);

    for (int i = 1; i < 4; ++i) {
        if (coeffs[0] < coeffs[i]) {
            coeffs[0] = coeffs[i];
            prof = i;
        }
    }

    return prof + 1;
}

//============================================================================
int get_bal_strength(const char_data& character)
{
    // dgurley:  I agree with the intent behind this function, but not it's implementation.
    // I don't think that there should be a racial normalization factor.  However, we could
    // treat all high strength scores with diminishing returns.  This function wouldn't be
    // the correct place to do that though - it would be in whatever function is using strength.

    static const int max_race_str[] = {
        22, // IMM
        22, // HUMAN
        22, // DWARF
        22, // WOOD
        20, // HOBBIT
        22, // HIGH ELF
        22,
        22,
        22,
        22,
        22,
        22, // URUK
        22, // HARAD
        22, // COMMON ORC
        22, // EASTERLING
        22, // LHUTH
        22,
        22,
        22,
        22,
        22, // TROLL
        22
    };

    // If the character's strength is within normal bounds for their race, just return it.
    int race = character.player.race;
    if (race > 21)
        return character.tmpabilities.str;

    int race_strength_cap = max_race_str[race];
    if (character.tmpabilities.str <= race_strength_cap)
        return character.tmpabilities.str;

    // Otherwise, only even points of strength count.
    int bounded_strength = race_strength_cap + ((character.tmpabilities.str - race_strength_cap) / 2);
    return bounded_strength;
}

//============================================================================
double get_bal_strength_d(const char_data& character)
{
    // dgurley:  I agree with the intent behind this function, but not it's implementation.
    // I don't think that there should be a racial normalization factor.  However, we could
    // treat all high strength scores with diminishing returns.  This function wouldn't be
    // the correct place to do that though - it would be in whatever function is using strength.

    static const double max_race_str[] = {
        22, // IMM
        22, // HUMAN
        22, // DWARF
        22, // WOOD
        20, // HOBBIT
        22, // HIGH ELF
        22,
        22,
        22,
        22,
        22,
        22, // URUK
        22, // HARAD
        22, // COMMON ORC
        22, // EASTERLING
        22, // LHUTH
        22,
        22,
        22,
        22,
        22, // TROLL
        22
    };

    // If the character's strength is within normal bounds for their race, just return it.
    int race = character.player.race;
    if (race > 21)
        return character.tmpabilities.str;

    double race_strength_cap = max_race_str[race];
    if (character.tmpabilities.str <= race_strength_cap)
        return character.tmpabilities.str;

    // Otherwise, only even points of strength count.
    double bounded_strength = race_strength_cap + ((character.tmpabilities.str - race_strength_cap) * 0.5);
    return bounded_strength;
}

//============================================================================
bool is_evil_race(const char_data& character)
{
    int race = character.player.race;
    return race == RACE_URUK || race == RACE_ORC || race == RACE_MAGUS || race == RACE_OLOGHAI || race == RACE_HARADRIM;
}

//============================================================================
// Internal helper namespace for constants, etc.
//============================================================================
namespace {
    /* Encumbrance values used for light fighting.  These values represent the
     * expected encumbrance for a piece of gear in that slot. */
    const int light_fighting_encumb_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        1, /*body*/
        1, /*head*/
        1, /*legs*/
        1, /*feet*/
        1, /*hands*/
        1, /*arms*/
        2, /*shield*/
        0, /*about body*/
        0, /*about waist*/
        0,
        0,
        2, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    /* Light fighting weight values.  These values represent the expected
     * weight of an item in this slot. */
    const int light_fighting_weight_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        225, /*body*/
        225, /*head*/
        225, /*legs*/
        225, /*feet*/
        225, /*hands*/
        225, /*arms*/
        500, /*shield*/
        50, /*about body*/
        0, /*about waist*/
        0,
        0,
        165, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    /* Encumbrance values used for heavy fighting.  These values represent the
     * expected encumbrance for a piece of gear in that slot. */
    const int heavy_fighting_encumb_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        2, /*body*/
        2, /*head*/
        2, /*legs*/
        2, /*feet*/
        2, /*hands*/
        2, /*arms*/
        3, /*shield*/
        0, /*about body*/
        0, /*about waist*/
        0,
        0,
        3, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    /* Weight values used for heavy fighting.  These values represent the
     * expected weight for a piece of gear in that slot. */
    const int heavy_fighting_weight_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        975, /*body*/
        325, /*head*/
        650, /*legs*/
        350, /*feet*/
        400, /*hands*/
        650, /*arms*/
        500, /*shield*/
        100, /*about body*/
        0, /*about waist*/
        0,
        0,
        250, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    //============================================================================
    // Returns the weight of items worn by the character, potentially multiplying
    // them by the encumbrance table.
    //============================================================================
    int get_character_item_weight(const char_data& character, const sh_int* encumb_table, int default_value)
    {
        game_types::player_specs spec = get_specialization(character);
        if (spec == game_types::PS_HeavyFighting) {
            int total_worn_weight = 0;
            for (int wear_index = 0; wear_index < MAX_WEAR; ++wear_index) {
                const obj_data* worn_item = character.equipment[wear_index];
                if (worn_item) {
                    int item_weight = worn_item->get_weight();
                    const int heavy_item_weight = heavy_fighting_weight_table[wear_index];
                    if (heavy_item_weight > 0 && item_weight > heavy_item_weight) {
                        // Heavy fighting uses the base item weight as the soft cap for worn weight.
                        int heavy_adjustment = item_weight - heavy_item_weight;
                        item_weight = heavy_item_weight + heavy_adjustment / 3;
                    }

                    if (encumb_table) {
                        int encumb_value = encumb_table[wear_index];
                        if (encumb_value > 0) {
                            total_worn_weight += item_weight * encumb_value;
                        } else {
                            total_worn_weight += item_weight / 2;
                        }
                    } else {
                        total_worn_weight += item_weight;
                    }
                }
            }

            return total_worn_weight;
        } else if (spec == game_types::PS_LightFighting) {
            int total_worn_weight = 0;
            for (int wear_index = 0; wear_index < MAX_WEAR; ++wear_index) {
                const obj_data* worn_item = character.equipment[wear_index];
                if (worn_item) {
                    int item_weight = worn_item->get_weight();
                    const int light_item_weight = light_fighting_weight_table[wear_index];

                    // Light fighting performs a soft weight reduction based on the item weight.
                    item_weight = std::max(item_weight - light_item_weight, 0);

                    if (encumb_table) {
                        int encumb_value = encumb_table[wear_index];
                        if (encumb_value > 0) {
                            total_worn_weight += item_weight * encumb_value;
                        } else {
                            total_worn_weight += item_weight / 2;
                        }
                    } else {
                        total_worn_weight += item_weight;
                    }
                }
            }

            return total_worn_weight;
        }

        return default_value;
    }

    //============================================================================
    // Returns the character's encumbrance value, calculated based on spec.
    //============================================================================
    int get_encumbrance(const char_data& character, const sh_int* encumb_table, int default_value)
    {
        if (get_specialization(character) == game_types::PS_HeavyFighting) {
            // Recalculate encumbrance.
            int new_encumb = 0;

            // Used to track how much the character is "over" the encumbrance difference.
            // [[maybe_unused]]: computed but not applied yet -- see the "Removing this for
            // now" comment below; kept for the documented future re-enable.
            [[maybe_unused]] int heavy_fighting_encumbrance_difference = 0;

            for (int item_index = 0; item_index < MAX_WEAR; ++item_index) {
                if (encumb_table[item_index] > 0) {
                    const obj_data* worn_item = character.equipment[item_index];
                    if (worn_item) {
                        sh_int multiplier = encumb_table[item_index];
                        int item_encumbrance = worn_item->obj_flags.value[2];

                        const int heavy_item_encumbrance = heavy_fighting_encumb_table[item_index];
                        if (heavy_item_encumbrance > 0 && item_encumbrance > heavy_item_encumbrance) {
                            heavy_fighting_encumbrance_difference += (item_encumbrance - heavy_item_encumbrance) * multiplier;
                            item_encumbrance = heavy_item_encumbrance;
                        }

                        new_encumb += item_encumbrance * multiplier;
                    }
                }
            }

            // Drelidan:  Removing this for now, but leaving it in for future changes.
            // new_encumb += int(std::sqrt(heavy_fighting_encumbrance_difference));

            return new_encumb;
        } else if (get_specialization(character) == game_types::PS_LightFighting) {
            // Recalculate encumbrance.
            int new_encumb = 0;

            for (int item_index = 0; item_index < MAX_WEAR; ++item_index) {
                if (encumb_table[item_index] > 0) {
                    const obj_data* worn_item = character.equipment[item_index];
                    if (worn_item) {
                        int item_encumbrance = worn_item->obj_flags.value[2];
                        item_encumbrance = std::max(item_encumbrance - light_fighting_encumb_table[item_index], 0);
                        new_encumb += item_encumbrance * encumb_table[item_index];
                    }
                }
            }

            return new_encumb;
        }

        return default_value;
    }
}

//============================================================================
// Calculates the new encumbrance weight for a character based on specialization.
//============================================================================
int get_encumbrance_weight(const char_data& character)
{
    sh_int* encumb_table = get_encumb_table();
    int encumbrance_weight = character.specials.encumb_weight;

    return get_character_item_weight(character, encumb_table, encumbrance_weight);
}

//============================================================================
// Calculates the new worn weight for a character based on specialization.
//============================================================================
int get_worn_weight(const char_data& character)
{
    int worn_weight = character.specials.worn_weight;

    return get_character_item_weight(character, NULL, worn_weight);
}

//============================================================================
int get_encumbrance(const char_data& character)
{
    int base_encumb = character.points.encumb;
    sh_int* encumb_table = get_encumb_table();
    return get_encumbrance(character, encumb_table, base_encumb);
}

//============================================================================
int get_leg_encumbrance(const char_data& character)
{
    int base_encumb = character.specials2.leg_encumb;
    sh_int* encumb_table = get_leg_encumb_table();
    return get_encumbrance(character, encumb_table, base_encumb);
}

//============================================================================
int get_skill_penalty(const char_data& character)
{
    const int encumb_multiplier = 25;
    const int encumb_divisor = 50;

    int character_strength = get_bal_strength(character);
    int encumb_weight = get_encumbrance_weight(character);
    int encumbrance = get_encumbrance(character);

    int raw_encumb_factor = encumbrance * encumb_multiplier;
    int encumb_weight_factor = encumb_weight / character_strength;
    int skill_penalty = raw_encumb_factor + encumb_weight_factor;
    skill_penalty /= encumb_divisor;
    return skill_penalty;
}

//============================================================================
int get_dodge_penalty(const char_data& character)
{
    const int dodge_multiplier = 20;

    int character_strength = get_bal_strength(character);
    int worn_weight = get_worn_weight(character);

    int raw_encumb_factor = get_leg_encumbrance(character) * dodge_multiplier;
    int worn_weight_factor = worn_weight / character_strength;
    int dodge_penalty = raw_encumb_factor + worn_weight_factor;
    dodge_penalty /= dodge_multiplier;
    return dodge_penalty;
}

//============================================================================
long get_idnum(const char_data& character)
{
    if (is_npc(character))
        return -1;

    return character.specials2.idnum;
}

//============================================================================
bool is_awake(const char_data& character)
{
    return character.specials.position > POSITION_SLEEPING;
}

//============================================================================
int get_ranking_tier(const char_data& character)
{
    return get_ranking_tier(character.player.ranking);
}

//============================================================================
int get_ranking_tier(int ranking)
{
    if (ranking <= 3)
        return ranking;

    return 4;
}

//============================================================================
// Functions in this namespace do not belong in this file and need to be moved
// elsewhere.
//============================================================================
namespace TEMPORARY {
    int ch_get_confuse_modifier(const char_data& character)
    {
        int modifier = 0;
        const affected_type* status_effect = character.affected;

        // Iterate through the character affects, look for confuse, and
        // use its duration to determine its strength.
        while (modifier == 0 && status_effect) {
            if (status_effect->type == SPELL_CONFUSE) {
                modifier = (status_effect->duration * 2) - 10;
            }

            status_effect = status_effect->next;
        }

        return modifier;
    }
}

//============================================================================
int get_raw_skill(const char_data& character, int skill_index)
{
    if (character.player.bodytype == 2)
        return 0;

    // TODO(dgurley):  This is the GET_RAW_SKILL macro as written.
    // Ensure it is correct.
    return get_knowledge(character, skill_index);
}

//============================================================================
int get_skill(const char_data& character, int skill_index)
{
    int raw_skill = get_raw_skill(character, skill_index);

    if (is_affected_by(character, AFF_CONFUSE)) {
        raw_skill -= TEMPORARY::ch_get_confuse_modifier(character);
    }

    return raw_skill;
}

//============================================================================
void set_skill(char_data& character, int skill_index, byte value)
{
    if (!character.skills.empty()) {
        character.skills[skill_index] = value;
    }
}

//============================================================================
int get_raw_knowledge(const char_data& character, int skill_index)
{
    if (character.knowledge.empty())
        return 80;

    return character.knowledge[skill_index];
}

//============================================================================
int get_knowledge(const char_data& character, int skill_index)
{
    int raw_knowledge = get_raw_knowledge(character, skill_index);

    if (is_affected_by(character, AFF_CONFUSE)) {
        raw_knowledge -= TEMPORARY::ch_get_confuse_modifier(character);
    }

    return raw_knowledge;
}

//============================================================================
void set_knowledge(char_data& character, int skill_index, byte value)
{
    if (!character.knowledge.empty()) {
        character.knowledge[skill_index] = value;
    }
}

//============================================================================
int get_carry_weight_limit(const char_data& character)
{
    const int base_carry_weight = 2000;
    const int strength_multiplier = 1000;

    return base_carry_weight + strength_multiplier * character.tmpabilities.str;
}

//============================================================================
int get_carry_item_limit(const char_data& character)
{
    const int base_items = 5;

    return base_items + (character.tmpabilities.dex / 2) + (character.player.level / 2);
}

//============================================================================
bool is_twohanded(const char_data& character)
{
    return utils::is_set(character.specials.affected_by, (long)AFF_TWOHANDED);
}

//============================================================================
bool can_carry_object(const char_data& character, const obj_data& object)
{
    bool can_lift_object = character.specials.carry_weight + object.obj_flags.weight < get_carry_weight_limit(character);
    bool has_hands_free = character.specials.carry_items + 1 < get_carry_item_limit(character);

    return can_lift_object && has_hands_free;
}

//============================================================================
bool can_see(const char_data& character, const weather_data& weather, const room_data& room)
{
    if (character.in_room == NOWHERE)
        return false;

    if (is_affected_by(character, AFF_BLIND))
        return false;

    if (is_player_flagged(character, PLR_WRITING))
        return false;

    if (is_shadow(character))
        return true;

    // need the current room for the character as well.
    if (utils::is_light(room, weather))
        return true;

    if (is_affected_by(character, AFF_INFRARED) || is_preference_flagged(character, PRF_HOLYLIGHT))
        return true;

    if (weather.moonlight > 0 && is_affected_by(character, AFF_MOONVISION) && utils::is_room_outside(room))
        return true;

    return true;
}

//============================================================================
bool can_see_object(const char_data& character, const obj_data& object, const weather_data& weather,
    const room_data& room)
{
    int item_flags = object.obj_flags.extra_flags;

    if (is_shadow(character)) {
        return utils::is_set(item_flags, ITEM_MAGIC)
            || utils::is_set(item_flags, ITEM_WILLPOWER);
    } else {
        return can_see(character, weather, room)
            && (!utils::is_set(item_flags, ITEM_INVISIBLE)
                || is_affected_by(character, AFF_DETECT_INVISIBLE));
    }
}

//============================================================================
bool can_get_object(const char_data& character, const obj_data& object, const weather_data& weather,
    const room_data& room)
{
    return utils::can_wear(object, ITEM_TAKE)
        && can_carry_object(character, object)
        && can_see_object(character, object, weather, room);
}

//============================================================================
bool is_shadow(const char_data& character)
{
    if (is_npc(character)) {
        return utils::is_set(character.specials2.act, (long)MOB_SHADOW);
    } else {
        return utils::is_set(character.specials2.act, (long)PLR_ISSHADOW);
    }
}

// utils::is_race_good(const char_data&) + its utils::is_race_good(int)
// sibling relocated to entity_lifecycle.cpp (entity-seed Task 6,
// controller-adjudicated relocation, third pass); declarations unchanged
// (char_utils.h).

//============================================================================
bool is_race_evil(const char_data& character)
{
    return is_race_evil(character.player.race);
}

//============================================================================
bool is_race_easterling(const char_data& character)
{
    return is_race_easterling(character.player.race);
}

// utils::is_race_magi(const char_data&) + its utils::is_race_magi(int)
// sibling relocated to entity_lifecycle.cpp (entity-seed Task 6,
// controller-adjudicated relocation, third pass); declarations unchanged
// (char_utils.h).

//============================================================================
bool is_race_haradrim(const char_data& character)
{
    return is_race_haradrim(character.player.race);
}

//============================================================================
bool is_race_evil(int race)
{
    return race > 10;
}

//============================================================================
bool is_race_easterling(int race)
{
    return race == 14;
}

//============================================================================
bool is_race_haradrim(int race)
{
    return race == 18;
}

//============================================================================
const char* get_object_string(const char_data& character, const obj_data& object,
    const weather_data& weather, const room_data& room)
{
    if (can_see_object(character, object, weather, room)) {
        return object.short_description;
    } else {
        return "something";
    }
}

//============================================================================
const char* get_object_name(const char_data& character, const obj_data& object,
    const weather_data& weather, const room_data& room)
{
    if (can_see_object(character, object, weather, room)) {
        // dgurley: fname isn't thread safe
        return fname(object.name);
    } else {
        return "something";
    }
}

//============================================================================
bool is_good(const char_data& character)
{
    return character.specials2.alignment >= 100;
}

//============================================================================
bool is_evil(const char_data& character)
{
    return character.specials2.alignment <= -100;
}

//============================================================================
bool is_neutral(const char_data& character)
{
    return !is_good(character) && !is_evil(character);
}

//============================================================================
bool is_hostile_to(const char_data& character, const char_data& victim)
{
    if (!is_npc(character))
        return false;

    // An NPC from the other side of the race war than the victim will be hostile.
    if (character.player.race != 0 && !is_npc(victim) && other_side(&character, &victim))
        return true;

    // An NPC is hostile if it has a preference for the victim's race set.
    if (is_preference_flagged(character, 1 << victim.player.race))
        return true;

    return false;
}

//============================================================================
bool is_rp_race_check(const char_data& character, const char_data& victim)
{
    return (is_npc(character) && character.specials2.rp_flag != 0) || utils::is_set(character.specials2.rp_flag, 1 << victim.player.race);
}

//============================================================================
bool is_riding(const char_data& character)
{
    return character.mount_data.mount && char_exists(character.mount_data.mount_number);
}

//============================================================================
bool is_ridden(const char_data& character)
{
    return character.mount_data.rider && char_exists(character.mount_data.rider_number);
}

//============================================================================
const char* get_prof_abbrev(const char_data& character)
{
    if (is_npc(character))
        return "--";

    static const std::string_view prof_abbrevs[] = {
        "--", "Mu", "Cl", "Ra", "Wa"
    };

    int prof = character.player.prof;
    return prof_abbrevs[prof].data();
}

//============================================================================
const char* get_race_abbrev(const char_data& character)
{
    if (is_npc(character))
        return "--";

    // dgurley:  Unsure the extra padding is necessary.
    // Just copying the table from consts.
    const std::string_view race_abbrevs[] = {
        "Imm",
        "Hum",
        "Dwf",
        "WdE",
        "Hob",
        "HiE",
        "Beo",
        "??",
        "??",
        "??",
        "??",
        "Urk",
        "Har",
        "Orc",
        "Eas",
        "Lhu",
        "??",
        "??",
        "??",
        "??",
        "Trl",
        "??"
    };

    int race = character.player.race;
    return race_abbrevs[race].data();
}

int get_race(const char_data& character)
{
    return character.player.race;
}

//============================================================================
// get_minimum_insight_perception() relocated to entity_lifecycle.cpp
// (entity-seed Task 5); declaration unchanged in char_utils.h.

//============================================================================
int get_race_perception(const char_data& character)
{
    int race = character.player.race;

    switch (race) {
    case RACE_GOD:
        return 0;
    case RACE_HUMAN:
        return 30;
    case RACE_DWARF:
        return 0;
    case RACE_WOOD:
        return 50;
    case RACE_HOBBIT:
        return 30;
    case RACE_HIGH:
        return 100;
    case RACE_BEORNING:
        return 15;
    case RACE_URUK:
        return 30;
    case RACE_HARAD:
        return 30;
    case RACE_ORC:
        return 10;
    case RACE_EASTERLING:
        return 30;
    case RACE_MAGUS:
        return 30;
    case RACE_UNDEAD:
        return 60;
    case RACE_TROLL:
        return 30;
    default:
        return 0;
    }

    return 0;
}

//============================================================================
int get_perception(const char_data& character)
{
    if (is_shadow(character))
        return 100;

    int perception = character.specials2.perception;
    if (character.specials2.perception == -1)
        return get_race_perception(character);

    // perception is clamped between 0 and 100.
    return std::min(std::max(perception, 0), 100);
}

//============================================================================
bool is_mental(const char_data& character)
{
    return is_shadow(character) || (!is_npc(character) && is_preference_flagged(character, PRF_MENTAL));
}

//============================================================================
// get_specialization() relocated to entity_lifecycle.cpp (entity-seed Task 5);
// declaration unchanged in char_utils.h.

//============================================================================
// set_specialization() relocated to entity_lifecycle.cpp (entity-seed Task 5);
// declaration unchanged in char_utils.h. Its track_specialized_mage()/
// untrack_specialized_mage() calls now resolve downward into output_seam.cpp
// (entity-seed Task 3).

//============================================================================
bool is_resistant(const char_data& character, int attack_group)
{
    return (character.specials.resistance & (1 << attack_group)) != 0;
}

//============================================================================
bool is_vulnerable(const char_data& character, int attack_group)
{
    return (character.specials.vulnerability & (1 << attack_group)) != 0;
}

//============================================================================
bool is_guardian(const char_data& character)
{
    return is_npc(character) && is_affected_by(character, AFF_CHARM)
        && character.master != NULL && is_mob_flagged(character, MOB_GUARDIAN);
}

//============================================================================
int get_energy_regen(const char_data& character)
{
    int regen = character.points.ENE_regen;

    // Dispatches to entity_hooks.h's wild-attack-speed-multiplier hook
    // (wild_fighting_handler.cpp registers the real construct-and-query,
    // const_cast included -- see that file's registered hook body).
    regen *= rots::entity::dispatch_wild_attack_speed_multiplier(&character);

    return regen;
}

}

//============================================================================
// Below here you will find various implementations from structs.h
// TODO(drelidan): Create a structs.cpp file and put all of this code there.
//============================================================================

//============================================================================
int char_data::get_spent_practice_count() const
{
    if (skills.empty())
        return 0;

    int count = 0;
    for (int index = 0; index < MAX_SKILLS; ++index) {
        count += skills[index];
    }

    return count;
}

//============================================================================
int char_data::get_max_practice_count() const
{
    const int free_pracs = 10;

    int base_pracs = player.level * PRACS_PER_LEVEL;
    int bonus_lea_pracs = player.level * get_max_lea() / LEA_PRAC_FACTOR;

    return base_pracs + bonus_lea_pracs + free_pracs;
}

//============================================================================
void char_data::update_available_practice_sessions()
{
    specials2.spells_to_learn = get_max_practice_count() - get_spent_practice_count();
}

//============================================================================
void char_data::reset_skills()
{
    if (skills.empty() || knowledge.empty())
        return;

    for (int index = 0; index < MAX_SKILLS; ++index) {
        skills[index] = 0;
        knowledge[index] = 0;
    }

    specials2.spells_to_learn = get_max_practice_count();
}

//============================================================================
bool char_data::is_affected() const
{
    return affected != NULL;
}

//============================================================================
// Specialization stuff! -- the entire cold_spec_data::on_*/specialization_data::
// set/specialization_data::to_string/elemental_spec_data::to_string+
// report_exposed_data/*_spec_data::to_string family relocated verbatim to
// entity_lifecycle.cpp (entity-seed Task 6, controller-adjudicated
// relocation): specialization_data::set() constructs each *_spec_data
// subclass, so each subclass's vtable -- emitted where its key function
// (to_string(), the first non-inline virtual) is defined -- must live in
// the same archive that constructs the objects. Declarations unchanged
// (rots/core/character.h).
//============================================================================
// Damage reporting stuff!
//============================================================================
std::string player_damage_details::get_damage_report(const char_data* character) const
{
    typedef std::map<int, damage_details>::const_iterator map_iter;

    const skill_data* skills = get_skill_array();

    const char* character_name = utils::get_name(*character);
    if (damage_map.empty()) {
        return std::format("{} has not recorded any damage dealt.\n", character_name);
    }

    /* First pass: Calculate total damage dealt.*/
    long total_damage_dealt = 0;
    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        total_damage_dealt += iter->second.get_total_damage();
    }

    std::string message_writer;
    std::format_to(std::back_inserter(message_writer), "Damage report details for {}:\n", character_name);
    message_writer.append("-------------------------------------------------------------------------------\n");

    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        // Longest real name in either source table (hit_text.singular from
        // attack_hit_text[], or skill.name from the skills[] table) is well
        // under the 24-char field width -- both tables live in consts.cpp
        // now -- so std::format's min-width padding never
        // truncates real data the way the old fixed char[25]+sprintf could
        // silently overflow on a hypothetical longer name.
        std::string ability_name;

        int ability_index = iter->first;
        if (ability_index > 128 && ability_index >= TYPE_HIT && ability_index < 152) {
            const attack_hit_type& hit_text = get_hit_text(ability_index);
            ability_name = std::format("{:<24}", hit_text.singular);
        } else {
            const skill_data& skill = skills[iter->first];
            // skill.name is a fixed char[50] array, not a char*: passed as-is,
            // std::format's argument capture preserves its array-ness (rather
            // than the implicit pointer decay an ordinary function call would
            // apply), and libc++ formats a char[N] argument as a raw N-element
            // sequence -- embedded/trailing NULs included -- rather than as a
            // NUL-terminated string, diverging from libstdc++'s behavior
            // (observed as a macOS-CI-only characterization failure: the
            // padded field came out as "swipe\0\0\0...<Count..." instead of
            // "swipe                   <Count..."). Decay to const char*
            // explicitly so every implementation selects the NUL-terminated-
            // string formatter.
            ability_name = std::format("{:<24}", static_cast<const char*>(skill.name));
        }

        const damage_details& details = iter->second;

        // Average/percentage both render with 2 fixed decimals, matching the
        // std::fixed + precision(2) stream state the prior stream-based
        // version applied for the remainder of the report (see the
        // Combat Time/DPS line below, which relies on the same rule).
        std::format_to(std::back_inserter(message_writer),
            "{}<Count: {}, Total: {}, Max: {}, Average: {:.2f}> {:.2f}% of damage\n", ability_name,
            details.get_instance_count(), details.get_total_damage(), details.get_largest_damage(),
            details.get_average_damage(),
            (details.get_total_damage() / double(total_damage_dealt)) * 100);
    }

    float combat_seconds = std::max(elapsed_combat_seconds, 0.5f);
    float dps = static_cast<float>(total_damage_dealt) / combat_seconds;
    message_writer.append("-------------------------------------------------------------------------------\n");
    std::format_to(std::back_inserter(message_writer),
        "Total Damage: {}; Combat Time: {:.2f}s; Damage per Second: {:.2f}\n", total_damage_dealt,
        combat_seconds, dps);
    message_writer.append("-------------------------------------------------------------------------------\n");
    return message_writer;
}

//============================================================================
std::string group_damaga_data::get_damage_report() const
{
    typedef std::map<char_data*, timed_damage_details>::const_iterator map_iter;

    if (damage_map.empty()) {
        return std::string("You have not recorded any damage dealt.\r\n");
    }

    /* First pass: Calculate total damage dealt.*/
    long total_damage_dealt = 0;
    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        total_damage_dealt += iter->second.get_total_damage();
    }

    std::string message_writer;
    message_writer.append("Group damage report details:\n");
    message_writer.append("-------------------------------------------------------------------------------\n");

    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        char_data* character = iter->first;
        const std::string character_name = std::format("{:<24}", utils::get_name(*character));

        const timed_damage_details& details = iter->second;

        // Average/DPS/percentage all render with 2 fixed decimals, matching
        // the std::fixed + precision(2) stream state the prior stream-based
        // version applied here.
        std::format_to(std::back_inserter(message_writer),
            "{}<Count: {}, Total: {}, Max: {}, Average: {:.2f}, DPS: {:.2f}> {:.2f}% of group damage\n",
            character_name, details.get_instance_count(), details.get_total_damage(),
            details.get_largest_damage(), details.get_average_damage(), details.get_dps(),
            (details.get_total_damage() / double(total_damage_dealt)) * 100);
    }

    message_writer.append("-------------------------------------------------------------------------------\n");
    std::format_to(std::back_inserter(message_writer), "Total Damage: {}\n", total_damage_dealt);
    message_writer.append("-------------------------------------------------------------------------------\n");
    return message_writer;
}

//============================================================================
// Group-based code!
//============================================================================
void group_data::add_member(char_data* member)
{
    if (std::find(members.begin(), members.end(), member) != members.end())
        return;

    members.push_back(member);
    member->group = this;

    if (utils::is_pc(*member)) {
        ++pc_count;
    }
}

bool group_data::remove_member(char_data* member)
{
    /* The leader cannot be removed from a group. */
    if (member == leader)
        return false;

    char_iter member_iter = std::remove(members.begin(), members.end(), member);
    if (member_iter == members.end())
        return false;

    members.erase(member_iter);
    damage_report.remove(member);
    member->group = NULL;

    if (utils::is_pc(*member)) {
        --pc_count;
    }

    return true;
}

//============================================================================
bool group_data::is_member(struct char_data* character) const
{
    return std::find(members.begin(), members.end(), character) != members.end();
}

//============================================================================
void group_data::get_pcs_in_room(char_vector& pc_vec, int room_number) const
{
    for (const_char_iter iter = members.begin(); iter != members.end(); ++iter) {
        char_data* character = *iter;
        if (utils::is_pc(*character) && character->in_room == room_number) {
            pc_vec.push_back(character);
        }
    }
}

//============================================================================
void group_data::reset_damage()
{
    damage_report.reset();

    for (char_iter character = members.begin(); character != members.end(); ++character) {
        send_to_char("The group damage meter has been reset.\r\n", *character);
    }
}

//============================================================================
void group_data::track_combat_time(char_data* character, float elapsed_seconds)
{
    damage_report.track_time(character, elapsed_seconds);
}

//============================================================================
namespace string_func {
bool equals(std::string_view first, std::string_view second)
{
    return rots::text::truncate_at_null(first) == rots::text::truncate_at_null(second);
}

bool is_null_or_empty(const char* a) { return !a || a[0] == '\0'; }

bool contains(std::string_view text, std::string_view search_text)
{
    text = rots::text::truncate_at_null(text);
    search_text = rots::text::truncate_at_null(search_text);
    return text.find(search_text) != std::string_view::npos;
}
}

//============================================================================
// circle_follow()/keyword_matches_char()/can_swim() relocated verbatim from
// handler.cpp (placement-seam Task 4; census verdict MOVE-OTHER-L2 -- see
// placement-census.md's handler.cpp table and task-4-report.md). All three
// are entity-pure (char_data field/macro logic only, no world[]/output/
// combat calls). Declarations stay in handler.h.

/* Check if making CH follow VICTIM will create an illegal */
/* Follow "Loop/circle"                                    */
char circle_follow(struct char_data* ch, struct char_data* victim, int)
{
    for (char_data* character = victim; character; character = character->master) {
        if (character == ch) {
            return (TRUE);
        }
    }

    return (FALSE);
}

int keyword_matches_char(struct char_data* ch, struct char_data* vict, char* keyword)
{
    int check;

    if (other_side(ch, vict)) {
        check = isname_nullable(keyword, pc_race_keywords[GET_RACE(vict)].data());
    } else
        check = isname_nullable(keyword, vict->player.name);

    return check;
}

int can_swim(struct char_data* ch)
{

    struct obj_data* tmpobj;
    int tmp;

    if (IS_SHADOW(ch))
        return TRUE;

    if (!IS_NPC(ch) && !PRF_FLAGGED(ch, PRF_SWIM))
        return FALSE;

    if (IS_NPC(ch))
        if (IS_SET(ch->specials2.act, MOB_CAN_SWIM) || IS_AFFECTED(ch, AFF_FLYING) || IS_SHADOW(ch))
            return TRUE;

    if (IS_AFFECTED(ch, AFF_SWIM))
        return TRUE;

    if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_SWIM) > 0)
        return TRUE;

    for (tmpobj = ch->carrying; tmpobj; tmpobj = tmpobj->next_content)
        if (tmpobj->obj_flags.type_flag == ITEM_BOAT)
            return TRUE;

    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if ((ch->equipment[tmp])->obj_flags.type_flag == ITEM_BOAT)
                return TRUE;

    return FALSE;
}
