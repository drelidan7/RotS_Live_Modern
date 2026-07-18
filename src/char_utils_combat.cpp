#include "char_utils_combat.h"
#include "char_utils.h"
#include "environment_utils.h"
#include "object_utils.h"
#include "utils.h"

#include "handler.h" // for affect_to_char and affected_by_spell
#include "spells.h"

#include "entity_hooks.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include <algorithm>
#include <assert.h>
#include <set>

namespace utils {
//============================================================================
// Returns a vector of all of the characters that are currently engaged with the
// character passed in.
//============================================================================
std::vector<char_data*> get_engaged_characters(const char_data* character, const room_data& room)
{
    std::set<char_data*> seen_fighters;
    std::vector<char_data*> engaged_fighters;

    // Add the person the character is fighting to the list.
    if (character->specials.fighting != NULL) {
        seen_fighters.insert(character->specials.fighting);
        engaged_fighters.push_back(character->specials.fighting);
    }

    // Add everyone in the room that is fighting the character to the list.
    for (char_data* other = room.people; other; other = other->next_in_room) {
        if (other->specials.fighting == character) {
            if (seen_fighters.insert(other->specials.fighting).second) {
                engaged_fighters.push_back(other->specials.fighting);
            }
        }
    }

    return engaged_fighters;
}

//============================================================================
bool is_victim_player(const char_data* victim)
{
    assert(victim);

    if (utils::is_pc(*victim)) {
        return true;
    } else if (utils::is_ridden(*victim)) {
        return is_victim_player(victim->mount_data.rider);
    } else if (victim->master) {
        return is_victim_player(victim->master);
    } else {
        return false;
    }
}

//============================================================================
char_data* get_controlling_player(char_data* character)
{
    assert(character);

    if (utils::is_pc(*character)) {
        return character;
    } else if (utils::is_ridden(*character)) {
        return get_controlling_player(character->mount_data.rider);
    } else if (character->master) {
        return get_controlling_player(character->master);
    } else {
        return NULL;
    }
}

//============================================================================
void on_attacked_character(char_data* attacker, char_data* victim)
{
    /* Give anger */
    if (victim && !IS_NPC(attacker) && (victim != attacker)) {
        affected_type affect;
        affected_type* existing_affect;

        bool is_long_anger = is_victim_player(victim);
        int duration = is_long_anger ? 5 : 2;
        existing_affect = affected_by_spell(attacker, SPELL_ANGER);
        if (existing_affect) {
            existing_affect->duration = duration;
        } else {
            affect.type = SPELL_ANGER;
            affect.duration = duration;
            affect.modifier = 0;
            affect.location = APPLY_NONE;
            affect.bitvector = 0;
            affect_to_char(attacker, &affect);
        }

        // Alert Big Brother that some PK is happening.
        if (is_long_anger) {
            char_data* attacked_player = get_controlling_player(victim);

            // Dispatches to entity_hooks.h's attacked-player hook (big_brother.cpp
            // registers a forwarder to game_rules::big_brother::instance()'s real body).
            rots::entity::dispatch_attacked_player(attacker, attacked_player);
        }
    }
}
}
