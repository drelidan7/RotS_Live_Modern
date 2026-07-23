/* This file deals with procedures relating to profs. */

#include "platdef.h"
#include "fp_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "player_limits.h"
#include "persist_hooks.h"
#include "profs.h"
#include "spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

#include "char_utils.h"
#include "account_management.h"
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <format>
#include <numeric>
#include <vector>

#include <iostream>
#include <sstream>
#include <string>

#define MAX_STATSUM 99
#define NUM_STATS 6

extern struct char_data* character_list;
extern int max_race_str[];
extern struct obj_data* object_list;
extern int class_HP(const char_data* character); // entity_lifecycle.cpp -- _INTERNAL::stat_assigner::organize()'s HP-based stat-order tiebreak below.

namespace {

bool should_defer_account_backed_birth_persistence(char_data* character)
{
    if (character == nullptr || character->desc == nullptr || *character->desc->account_name == '\0')
        return false;

    switch (character->desc->connected) {
    case CON_QSEX:
    case CON_QRACE:
    case CON_QPROF:
    case CON_CREATE:
    case CON_CREATE2:
    case CON_COLOR:
    case CON_LATIN:
        return true;
    default:
        return false;
    }
}

}

struct prof_type existing_profs[DEFAULT_PROFS] = {
    { 'm', { 0, 100, 25, 16, 9 } },
    { 't', { 0, 25, 100, 9, 16 } },
    { 'r', { 0, 16, 9, 100, 25 } },
    { 'w', { 0, 9, 16, 25, 100 } },
    { 'n', { 0, 64, 64, 9, 13 } },
    { 'i', { 0, 121, 16, 9, 4 } },
    { 'h', { 0, 25, 121, 0, 4 } },
    { 's', { 0, 9, 13, 64, 64 } },
    { 'b', { 0, 0, 4, 25, 121 } },
    { 'a', { 0, 36, 36, 36, 42 } },
};

sh_int race_modifiers[MAX_RACES][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // God
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // Human
    { 2, 0, -2, -3, 4, -1, 0, 0 }, // Dwarf
    { -1, 1, 0, 2, -2, 0, 0, 0 }, // Wood Elf
    { -3, -1, 0, 2, 2, 0, 0, 0 }, // Hobbit
    { 0, 2, 0, 2, -2, 0, 0, 0 }, // High-Elf
    { 4, -4, -2, 0, 4, -2, 0, 0 }, // Beorning
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // Unused
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // Unused
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // Unused
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // Unsed
    { 0, -4, -3, 0, 2, -3, 0, 0 }, // Uruk-Hai
    { 0, 0, -1, 0, 1, 0, 0, 0 }, // !NPC - Harad!
    { -1, -3, -3, -1, -1, -5, 0, 0 }, // Common Orc
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // !NPC - Easterling!
    { -1, -1, -3, 0, 1, -2, 0, 0 }, // Uruk-Lhuth
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // !NPC - Undead!
    { 4, -4, -4, -3, 4, -3, 0, 0 }, // Olog-Hai
    { 0, -2, -2, 2, 0, -3, 0, 0 }, // Haradrim
    { 0, 0, 0, 0, 0, 0, 0, 0 }, // Unused
    { 0, 0, 0, 0, 0, 0, 0, 0 } // !NPC - Troll!
};

sh_int get_str_mod(int race)
{
    const int mod_index = 0;
    if (race > MAX_RACES)
        return 0;

    return race_modifiers[race][mod_index];
}

sh_int get_int_mod(int race)
{
    const int mod_index = 1;
    if (race > MAX_RACES)
        return 0;

    return race_modifiers[race][mod_index];
}

sh_int get_wil_mod(int race)
{
    const int mod_index = 2;
    if (race > MAX_RACES)
        return 0;

    return race_modifiers[race][mod_index];
}

sh_int get_dex_mod(int race)
{
    const int mod_index = 3;
    if (race > MAX_RACES)
        return 0;

    return race_modifiers[race][mod_index];
}

sh_int get_con_mod(int race)
{
    const int mod_index = 4;
    if (race > MAX_RACES)
        return 0;

    return race_modifiers[race][mod_index];
}

sh_int get_lea_mod(int race)
{
    const int mod_index = 5;
    if (race > MAX_RACES)
        return 0;

    return race_modifiers[race][mod_index];
}

// do_squareroot() (the recalc_abilities()-only overload) relocated to
// entity_lifecycle.cpp alongside recalc_abilities() (db-split Task 4b).

// class_HP() relocated to entity_lifecycle.cpp (entity-seed Task 5),
// preserving its strong (non-inline) definition linkage from db-split
// Task 4b's IFNDR fix; _INTERNAL::stat_assigner::organize() below still
// calls it, now via the extern declaration above.

void draw_line(char* buf, int length)
{
    int k;
    char buff[81];

    for (k = 0; k < length; k++)
        buff[k] = '*';
    buff[k] = 0;
    strcat(buf, buff);
}

void draw_coofs(char* buf, struct char_data* ch)
{
    char buf2[80];

    strcpy(buf, std::format("\r\n"
                             "    0%,      20%,      40%,      60%,      80%,      100%"
                             "\n\r"
                             "    |         |         |         |         |         |\n\r")
                    .c_str());

    strcpy(buf2, "Mag: ");
    draw_line(buf2, GET_PROF_COOF(1, ch) / 20);
    strcat(buf, buf2);

    strcpy(buf2, "\n\rMys: ");
    draw_line(buf2, GET_PROF_COOF(2, ch) / 20);
    strcat(buf, buf2);

    strcpy(buf2, "\n\rRan: ");
    draw_line(buf2, GET_PROF_COOF(3, ch) / 20);
    strcat(buf, buf2);

    strcpy(buf2, "\n\rWar: ");
    draw_line(buf2, GET_PROF_COOF(4, ch) / 20);
    strcat(buf, buf2);
    strcat(buf, "\n\r\0");
}

int points_used(char_data* character)
{
    return GET_PROF_POINTS(PROF_MAGE, character) + GET_PROF_POINTS(PROF_CLERIC, character) + GET_PROF_POINTS(PROF_RANGER, character) + GET_PROF_POINTS(PROF_WARRIOR, character);
}

void advance_level_prof(int prof, char_data* character)
{
    SET_PROF_LEVEL(prof, character, GET_PROF_LEVEL(prof, character) + 1);
    switch (prof) {
    case PROF_MAGE:
        GET_MAX_MANA(character) += 2;
        send_to_char("You feel more adept in magic!\n\r", character);
        break;
    case PROF_CLERIC: {
        send_to_char("Your spirit grows stronger!\n\r", character);

        // When the player's mystic level increases, scale their guardian if they are guardian spec.
        // Ensure that the guardian isn't healed to full in this case.
        if (utils::get_specialization(*character) == game_types::PS_Guardian) {
            int race_number = character->player.race;
            for (follow_type* follower = character->followers; follower; follower = follower->next) {
                int guardian_number = get_guardian_type(race_number, follower->follower);
                if (guardian_number != INVALID_GUARDIAN) {
                    bool restore_guardian_health = false;
                    // void, matching mystic.cpp's definition (see objsave.cpp's
                    // identical fix -- MSVC mangles the return type, GCC/Clang
                    // don't; Phase 3 Task 6).
                    extern void scale_guardian(int, const char_data*, char_data*, bool);
                    scale_guardian(guardian_number, character, follower->follower, restore_guardian_health);
                    break;
                }
            }
        }
    }

    break;
    case PROF_RANGER:
        send_to_char("You feel more agile!\n\r", character);
        break;
    case PROF_WARRIOR:
        send_to_char("You have become better at combat!\n\r", character);
        break;
    }
}

namespace {
int get_statsum(const char_data& character)
{
    return character.constabilities.con + character.constabilities.intel + character.constabilities.wil + character.constabilities.dex + character.constabilities.str + character.constabilities.lea;
}

int get_statsum_probability_modifier(int current_statsum)
{
    if (current_statsum > 97) {
        return 7;
    } else if (current_statsum > 96) {
        return 10;
    } else if (current_statsum > 95) {
        return 13;
    } else if (current_statsum > 93) {
        return 16;
    } else if (current_statsum > 91) {
        return 19;
    } else if (current_statsum > 86) {
        return 22;
    } else {
        return 25;
    }
}

// Determines if/how much additional chance a character has to get a strength
// hike.
int get_hike_bonus(const char_data& character, int profession)
{
    const int LEFTOVER_POINTS = 16;

    bool is_primary_prof = true;
    int num_equal_prof = 0;

    int prof_coofs = utils::get_prof_coof(profession, character);
    for (int prof_index = PROF_MAGIC_USER; prof_index <= MAX_PROFS; ++prof_index) {
        int cur_prof_points = utils::get_prof_coof(prof_index, character);
        if (cur_prof_points > prof_coofs) {
            is_primary_prof = false;
            break;
        } else if (cur_prof_points == prof_coofs) {
            num_equal_prof++;
        }
    }

    // Only our primary profession gets a hike bonus.
    if (!is_primary_prof)
        return 0;

    return LEFTOVER_POINTS / num_equal_prof;
}

/* Gain maximum in various points */
void check_stat_increase(char_data* character)
{
    if (!character)
        return;

    if (character->player.level <= 6)
        return;

    int statsum = get_statsum(*character);
    int race_index = character->player.race;
    for (int i = 0; i < 6; i++) {
        statsum -= race_modifiers[race_index][i];
    }

    // Since this is a > check, and not >=, players can get statsums of MAX_STATSUM + 1
    if (statsum > MAX_STATSUM)
        return;

    int statsum_roll = number(0, 99);
    int statsum_difference = MAX_STATSUM - statsum;
    int triple_difference = statsum_difference * 3;
    int target_number = triple_difference / 2;

    target_number += get_statsum_probability_modifier(statsum);
    if (statsum_roll > target_number)
        return;

    /* so now decide which stat to add */
    const int STAT_CHANCE = 14;

    int roll = number(0, 99);

    roll -= STAT_CHANCE;
    roll -= get_hike_bonus(*character, PROF_WARRIOR);
    if (roll < 0) {
        send_to_char("Great strength flows through you!\n", character);
        rots::persist::dispatch_exploit_capture(EXPLOIT_STAT, character, GET_LEVEL(character), "+1 str");
        character->constabilities.str++;
        character->tmpabilities.str++;
        return;
    }

    roll -= STAT_CHANCE;
    roll -= get_hike_bonus(*character, PROF_RANGER);
    if (roll < 0) {
        send_to_char("Your hands feel quicker!\n", character);
        rots::persist::dispatch_exploit_capture(EXPLOIT_STAT, character, GET_LEVEL(character), "+1 dex");
        character->constabilities.dex++;
        character->tmpabilities.dex++;
        return;
    }

    roll -= STAT_CHANCE;
    roll -= get_hike_bonus(*character, PROF_CLERIC);
    if (roll < 0) {
        send_to_char("You feel your mental resolve harden!\n", character);
        rots::persist::dispatch_exploit_capture(EXPLOIT_STAT, character, GET_LEVEL(character), "+1 will");
        character->constabilities.wil++;
        character->tmpabilities.wil++;
        return;
    }

    roll -= STAT_CHANCE;
    roll -= get_hike_bonus(*character, PROF_MAGE);
    if (roll < 0) {
        send_to_char("Your intelligence has improved!\n", character);
        rots::persist::dispatch_exploit_capture(EXPLOIT_STAT, character, GET_LEVEL(character), "+1 int");
        character->constabilities.intel++;
        character->tmpabilities.intel++;
        return;
    }

    roll -= STAT_CHANCE;
    if (roll < 0) {
        send_to_char("You feel much more health!\n", character);
        rots::persist::dispatch_exploit_capture(EXPLOIT_STAT, character, GET_LEVEL(character), "+1 con");
        character->constabilities.con++;
        character->tmpabilities.con++;
        return;
    }

    roll -= STAT_CHANCE;
    if (roll < 0) {
        send_to_char("You seem more learned!\n", character);
        rots::persist::dispatch_exploit_capture(EXPLOIT_STAT, character, GET_LEVEL(character), "+1 learn");
        character->constabilities.lea++;
        character->tmpabilities.lea++;
        return;
    }
}
} // end anonymous helper namespace

void check_for_special_levels(char_data* character)
{
    switch (GET_LEVEL(character)) {
    case 6:
        send_to_char("You are now able to see your statistics and reroll them!\n\rType stat to see your current statistics or type tell angel reroll to change them.\n\r", character);
        break;
    case 12:
        send_to_char("You are now able to pick a specialization!\n\rSee man spec all.\n\r", character);
        break;
    case 20:
        send_to_char("You are now able to set your title!\n\rSee man gen title.\n\r", character);
        break;
    case 30:
        send_to_char("You are now a legend character!\n\rSee man gen legend.\n\r", character);
        break;
    default:
        break;
    }
}

void advance_level(char_data* character)
{
    const bool defer_account_birth_persistence = should_defer_account_backed_birth_persistence(character);

    send_to_char("You feel more powerful!\n\r", character);

    if (GET_LEVEL(character) >= LEVEL_IMMORT) {
        for (int condition_index = 0; condition_index < 3; condition_index++) {
            GET_COND(character, condition_index) = (unsigned char)-1;
        }
        GET_RACE(character) = RACE_GOD;
    }

    // LOCAL-COMPOSITION (combat-trio wave Task 3, profs buf retirement):
    // the shared global buf (db_boot.cpp) is retired for this one
    // write-then-read idiom in favor of a local string; mudlog() already
    // takes std::string_view, so the strcpy/.c_str() round-trip through
    // the global is dropped entirely. Byte-identical log output.
    const std::string level_advance_message = std::format("{} advanced to level {}", GET_NAME(character), GET_LEVEL(character));
    mudlog(level_advance_message, BRF, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);

    /* log following levels in exploits */
    if (!defer_account_birth_persistence && ((GET_LEVEL(character) == 6) || ((GET_LEVEL(character) > 6) && !(GET_LEVEL(character) % 5))))
        rots::persist::dispatch_exploit_capture(EXPLOIT_LEVEL, character, GET_LEVEL(character), NULL);

    /* add birth exploit */
    if (!defer_account_birth_persistence && GET_LEVEL(character) == 1) {
        rots::persist::dispatch_exploit_capture(EXPLOIT_BIRTH, character, 0, NULL);
    }

    if (GET_LEVEL(character) > 5 && GET_MAX_MINI_LEVEL(character) < 600) {
        GET_REROLLS(character)
        ++;
        roll_abilities(character, 80, 93);
    }

    if (GET_MAX_MINI_LEVEL(character) < GET_MINI_LEVEL(character)) {
        check_stat_increase(character);
    }

    check_for_special_levels(character);

    character->update_available_practice_sessions();

    if (!defer_account_birth_persistence)
        save_char(character, NOWHERE, 0);
}

namespace {
// Returns a value between 3 and 18 for a stat roll.
int roll_stat()
{
    // Roll 4d6's, and drop the lowest to determine the stat.
    int roll_sum = 0;
    int lowest_roll = 6;
    for (int i = 0; i < 4; i++) {
        int roll = number(1, 6);
        roll_sum += roll;

        lowest_roll = std::min(lowest_roll, roll);
    }

    int stat_value = roll_sum - lowest_roll;
    return stat_value;
}

// Returns a list with num_stats stats (ranging from 3-18).
void roll_stats(int num_stats, std::vector<int>& stat_array)
{
    for (int i = 0; i < num_stats; i++) {
        int roll = roll_stat();
        stat_array[i] = roll;
    }
}

// Returns a valid stat array, ordered from lowest-to-highest.
// All stats will be between 3 and 18.  The stat sum will be between
// min and max (inclusive).
std::vector<int> get_stat_array(int num_stats, int sum_min, int sum_max, int)
{
    assert(sum_min <= sum_max);

    std::vector<int> rolled_stats;
    rolled_stats.resize(NUM_STATS);
    roll_stats(num_stats, rolled_stats);

    int stat_sum = std::accumulate(rolled_stats.begin(), rolled_stats.end(), 0);
    while (stat_sum > sum_max || stat_sum < sum_min) {
        roll_stats(num_stats, rolled_stats);
        stat_sum = std::accumulate(rolled_stats.begin(), rolled_stats.end(), 0);
    }

    std::sort(rolled_stats.begin(), rolled_stats.end());

    return rolled_stats;
}

// Struct for associating a prof with the number of points spent in it.
struct prof_coof_pair {
    prof_coof_pair()
        : prof(0)
        , prof_coof(0) {};
    prof_coof_pair(int in_prof, int coof)
        : prof(in_prof)
        , prof_coof(coof) {};

    int prof;
    int prof_coof;
};

// Operator overrides for std::sort algorithm.
bool operator<(const prof_coof_pair& a, const prof_coof_pair& b)
{
    return a.prof_coof < b.prof_coof;
}

bool operator==(const prof_coof_pair& a, const prof_coof_pair& b)
{
    return a.prof_coof == b.prof_coof;
}

} // end anonymous helper namespace

namespace _INTERNAL {
const int HEALTH_PROF_CUTOFF = 3000;

enum RotS_Stats {
    Strength,
    Intelligence,
    Will,
    Dexterity,
    Constitution,
    Learning,
    Invalid,
};

const char* get_stat_name(RotS_Stats stat)
{
    switch (stat) {
    case _INTERNAL::Strength:
        return "Strength";
    case _INTERNAL::Intelligence:
        return "Intelligence";
    case _INTERNAL::Will:
        return "Will";
    case _INTERNAL::Dexterity:
        return "Dexterity";
    case _INTERNAL::Constitution:
        return "Constitution";
    case _INTERNAL::Learning:
        return "Learning";
    case _INTERNAL::Invalid:
        return "Invalid";
    default:
        assert(false);
        return "";
    }
}

RotS_Stats get_primary_stat(int class_prof)
{
    switch (class_prof) {
    case PROF_MAGE:
        return _INTERNAL::Intelligence;
    case PROF_CLERIC:
        return _INTERNAL::Will;
    case PROF_RANGER:
        return _INTERNAL::Dexterity;
    case PROF_WARRIOR:
        return _INTERNAL::Strength;
    default:
        return _INTERNAL::Invalid;
    }
}

const char* get_prof_name(int class_prof)
{
    switch (class_prof) {
    case PROF_MAGE:
        return "Mage";
    case PROF_CLERIC:
        return "Mystic";
    case PROF_RANGER:
        return "Ranger";
    case PROF_WARRIOR:
        return "Warrior";
    default:
        return "Invalid";
    }
}

struct stat_assigner {
public:
    // Constructor for the stat_assigner.  This creates the order that
    // stats will be assigned in for a character.
    stat_assigner(char_data& character)
        : m_character(character)
    {
        // Array that contains the profs and their points for a character.
        prof_coof_pair profs[MAX_PROFS];
        for (int prof = PROF_MAGE, i = 0; prof <= MAX_PROFS && i < MAX_PROFS; ++prof, ++i) {
            profs[i] = prof_coof_pair(prof, utils::get_prof_coof(prof, character));
        }

        // Sort profs in order from least to greatest.
        std::sort(profs, profs + MAX_PROFS);

        int cur_stat_index = 0;
        const int CON_STAT_INDEX = 1;
        const int LEA_STAT_INDEX = 3;

        // Organize stat order based on class order.
        for (int i = 0; i < MAX_PROFS; ++i) {
            // Look-ahead search to see how many equal profs we have.
            int equal_profs = 0;
            for (int j = i; j < MAX_PROFS; ++j) {
                if (profs[i] == profs[j]) {
                    equal_profs++;
                }
            }

            // If there's more than one class with the same coofs, randomly determine
            // which class will be assigned now.  The swap and look-ahead nature
            // ensure that each stat will only be assigned once.
            if (equal_profs > 1) {
                int prof_to_assign = number(0, equal_profs - 1);
                std::swap(profs[i], profs[i + prof_to_assign]);
            }

            // Don't assign anything to these indices.
            if (cur_stat_index == CON_STAT_INDEX || cur_stat_index == LEA_STAT_INDEX)
                ++cur_stat_index;

            RotS_Stats class_primary_stat = get_primary_stat(profs[i].prof);

            m_stat_order[cur_stat_index++] = class_primary_stat;
        }

        // Constitution and Learning Ability are not the primary stats for any class.
        m_stat_order[CON_STAT_INDEX] = _INTERNAL::Constitution;
        m_stat_order[LEA_STAT_INDEX] = _INTERNAL::Learning;

        // Constitution and learning ability will be the 3rd and 5th highest stats.
        // Which is third and which is fifth depends on the character's "class_HP" value.
        // NOTE:  m_stat_pointers is sorted lowest to highest.
        bool prefersCon = class_HP(&character) >= HEALTH_PROF_CUTOFF;
        if (prefersCon) {
            std::swap(m_stat_order[CON_STAT_INDEX], m_stat_order[LEA_STAT_INDEX]);
        }
    }

    void assign_stats(int sum_min, int sum_max, int num_tries)
    {
        int race = m_character.player.race;

        // Stats are assigned in order from lowest to highest based on the
        // preferences of the character.
        std::vector<int> stat_rolls = get_stat_array(NUM_STATS, sum_min, sum_max, num_tries);
        for (int stat_index = 0; stat_index < NUM_STATS; stat_index++) {
            int stat_roll = stat_rolls[stat_index];
            RotS_Stats stat_type = m_stat_order[stat_index];

            signed char stat_value = (signed char)(stat_roll + 1);
            switch (stat_type) {
            case _INTERNAL::Strength: {
                int stat_mod = get_str_mod(race);
                m_character.constabilities.str = (signed char)std::max(stat_value + stat_mod, 1);
            } break;
            case _INTERNAL::Intelligence: {
                int stat_mod = get_int_mod(race);
                m_character.constabilities.intel = (signed char)std::max(stat_value + stat_mod, 1);
            } break;
            case _INTERNAL::Will: {
                int stat_mod = get_wil_mod(race);
                m_character.constabilities.wil = (signed char)std::max(stat_value + stat_mod, 1);
            } break;
            case _INTERNAL::Dexterity: {
                int stat_mod = get_dex_mod(race);
                m_character.constabilities.dex = (signed char)std::max(stat_value + stat_mod, 1);
            } break;
            case _INTERNAL::Constitution: {
                int stat_mod = get_con_mod(race);
                m_character.constabilities.con = (signed char)std::max(stat_value + stat_mod, 1);
            } break;
            case _INTERNAL::Learning: {
                int stat_mod = get_lea_mod(race);
                m_character.constabilities.lea = (signed char)std::max(stat_value + stat_mod, 1);
            } break;
            case _INTERNAL::Invalid:
                assert(false);
                break;
            default:
                break;
            }
        }
    }

private:
    RotS_Stats m_stat_order[NUM_STATS];
    char_data& m_character;
};
} // end _INTERNAL namespace

/* Give pointers to the six abilities */
void roll_abilities(char_data* character, int min_sum, int max_sum)
{
    _INTERNAL::stat_assigner statter(*character);
    statter.assign_stats(min_sum, max_sum, 1000);

    if (character->player.level > 1) {
        const char_ability_data& abils = character->constabilities;
        const char* character_name = utils::get_name(*character);
        log(std::format("STATS: {} rolled  {} {} {} {} {} {}",
            character_name, abils.str, abils.intel,
            abils.wil, abils.dex,
            abils.con, abils.lea)
                );
    }

    recalc_abilities(character);

    character->tmpabilities = character->abilities;
}

// recalc_abilities() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in utils.h.

/* Hp per level:  con/6 for pure mage, con/3 for normal warrior. plus*/
/* 10 hits for pure warrior */
/* and initial 10 hits for pure mage, 20 for warrior */
