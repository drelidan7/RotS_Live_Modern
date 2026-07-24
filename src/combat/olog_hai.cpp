#include <format>
#include <iostream>
#include <string>
#include <string.h>

#include "base_utils.h"
#include "char_utils.h"
#include "combat_hooks.h"
#include "comm.h"
#include "db.h"
#include "entity_hooks.h"
#include "handler.h"
#include "skill_timer.h"
#include "spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "utils.h"

extern struct room_data world;
extern struct char_data* waiting_list;
extern struct skill_data skills[];
void appear(struct char_data* ch);
int check_overkill(struct char_data* ch);
const int constexpr FRENZY_TIMER = 600;
const int constexpr SMASH_TIMER = 120;
const int constexpr STOMP_TIMER = 120;
const int constexpr CLEAVE_TIMER = 60;
const int constexpr OVERRUN_TIMER = 120;

namespace olog_hai {
int get_prob_skill(char_data* attacker, char_data* victim, int skill)
{
    int prob = utils::get_skill(*attacker, skill);
    prob -= get_real_dodge(victim) / 2;
    prob -= get_real_parry(victim) / 2;
    prob += get_real_OB(attacker) / 2;
    prob += number(1, 100);
    prob -= 120;
    return prob;
}

void apply_victim_delay(char_data* victim, int delay)
{
    if (IS_NPC(victim) && MOB_FLAGGED(victim, MOB_NOBASH)) {
        return;
    }
    if (IS_SET(victim->specials.affected_by, AFF_BASH)) {
        return;
    }
    WAIT_STATE_FULL(victim, delay, CMD_BASH, 2, 80, 0, 0, 0, AFF_WAITING | AFF_BASH, TARGET_IGNORE);
}

bool is_skill_valid(char_data* ch, const int& skill_id)
{
    if (utils::get_race(*ch) != RACE_OLOGHAI) {
        send_to_char("Unrecognized command.\r\n", ch);
        return false;
    }

    if (utils::is_shadow(*ch)) {
        send_to_char("Hmm, perhaps you've spent to much time in the mortal lands.\r\n", ch);
        return false;
    }

    const room_data& room = *room_of(ch);
    if (utils::is_set(room.room_flags, (long)PEACEROOM)) {
        send_to_char("A peaceful feeling overwhelms you, and you cannot bring "
                     "yourself to attack.\r\n",
            ch);
        return false;
    }

    if (utils::get_skill(*ch, skill_id) == 0) {
        std::string message;
        switch (skill_id) {
        case SKILL_SMASH:
            message = "Learn how to smash first.\r\n";
            break;
        case SKILL_FRENZY:
            message = "Learn how to frenzy first.\r\n";
            break;
        case SKILL_STOMP:
            message = "Learn how to stomp first.\r\n";
            break;
        case SKILL_CLEAVE:
            message = "Learn how to cleave first.\r\n";
            break;
        case SKILL_OVERRUN:
            message = "Learn how to overrun first.\r\n";
            break;
        }

        send_to_char(message, ch);
        return false;
    }

    obj_data* weapon = ch->equipment[WIELD];
    if (!weapon) {
        send_to_char("Wield a weapon first!\r\n", ch);
        return false;
    }
    return true;
}

char_data* is_target_valid(char_data* attacker, waiting_type* target)
{
    char_data* victim = nullptr;

    if (target->targ1.type == TARGET_TEXT) {
        victim = get_char_room_vis(attacker, target->targ1.ptr.text->text);
    } else if (target->targ1.type == TARGET_CHAR) {
        if (char_exists(target->targ1.ch_num)) {
            victim = target->targ1.ptr.ch;
        }
    }

    return victim;
}

bool is_target_in_room(char_data* attacker, char_data* victim)
{
    return location_of(attacker) == location_of(victim);
}

char_data* is_smash_target_valid(char_data* attacker, waiting_type* target)
{
    char_data* victim = is_target_valid(attacker, target);

    if (victim == nullptr) {
        if (attacker->specials.fighting) {
            victim = attacker->specials.fighting;
        } else {
            send_to_char("Smash who?\r\n", attacker);
            return nullptr;
        }
    }

    if (!is_target_in_room(attacker, victim)) {
        send_to_char("Your victim is no longer here.\r\n", attacker);
        return nullptr;
    }

    if (!CAN_SEE(attacker, victim)) {
        send_to_char("Smash who?\r\n", attacker);
        return nullptr;
    }

    if (attacker == victim) {
        send_to_char("Funny aren't we?\r\n", attacker);
        return nullptr;
    }

    return victim;
}

void do_sanctuary_check(char_data* ch)
{
    if (utils::is_affected_by(*ch, AFF_SANCTUARY)) {
        appear(ch);
        send_to_char("You cast off your santuary!\r\n", ch);
        act("$n renouces $s sanctuary!", FALSE, ch, 0, 0, TO_ROOM);
    }
}

char_data* get_random_target(char_data* ch, char_data* original_victim)
{
    int num = 0;
    for (auto* occ : rots::entity::occupants(room_of(ch))) {
        if (occ != ch && occ != original_victim)
            num++;
    }

    if (!num) {
        return original_victim;
    }

    num = number(1, num);
    char_data* found = nullptr;
    for (auto* occ : rots::entity::occupants(room_of(ch))) {
        if (occ != ch && occ != original_victim) {
            --num;
            if (!num) {
                found = occ;
                break;
            }
        }
    }
    return found;
}

int get_base_skill_damage(char_data& olog_hai, int prob)
{
    int base_damage = (2 + utils::get_prof_level(PROF_WARRIOR, olog_hai));
    base_damage *= (100 + prob);
    base_damage /= (1000 / utils::get_tactics(olog_hai));
    if (utils::is_twohanded(olog_hai)) {
        // Apply the two-handed 1.5x bonus (multiply first, then truncate) so it
        // actually takes effect; `base_damage *= 3 / 2` was an integer no-op.
        base_damage = base_damage * 3 / 2;
    }

    if (utils::is_affected_by_spell(olog_hai, SKILL_FRENZY)) {
        base_damage *= 1.10;
    }

    return base_damage;
}

int calculate_overrun_damage(char_data& attacker, int prob)
{
    int damage = get_base_skill_damage(attacker, prob);
    if (utils::get_specialization(attacker) == game_types::PS_HeavyFighting) {
        damage *= 1.10;
    }

    if (utils::is_riding(attacker)) {
        damage *= 1.25;
    }

    return damage;
}

int calculate_smash_damage(char_data& attacker, int prob)
{
    int damage = get_base_skill_damage(attacker, prob);
    if (utils::get_specialization(attacker) == game_types::PS_WildFighting) {
        damage += 5;
    }
    return damage;
}

// buf retirement (combat-trio wave, Task 1; trio-task-1-brief.md Step 4 /
// CONTROLLER ADDENDUM item 4; combat-trio-census.md section 5.8 -- 3 of the
// file's 10 genuine `buf` sites). LOCAL-COMPOSITION only: a local
// std::string replaces the shared global `buf` scratch buffer, read
// directly by act()'s std::string_view parameter (fight.cpp death_cry()
// precedent, combat-pilot wave Task 4a) -- no strcpy/global scratch needed.
// Output strings byte-identical.
void generate_smash_dismount_messages(char_data* attacker, char_data* victim)
{
    std::string message = "You smash into $N so hard, it knocks $M to the ground!";
    act(message, FALSE, attacker, NULL, victim, TO_CHAR);
    message = "$n smashes into you so hard, it knocks prone to the ground!";
    act(message, FALSE, attacker, NULL, victim, TO_VICT);
    message = "$n smashes into $N so hard, it knocks $M to the ground!";
    act(message, TRUE, attacker, 0, victim, TO_NOTVICT);
}

void apply_smash_damage(char_data* attacker, char_data* victim, int prob)
{
    int dam = calculate_smash_damage(*attacker, prob);
    if (IS_RIDING(victim) && number() >= 0.80) {
        dam *= 1.25;
        char_data* mount = victim->mount_data.mount;
        damage(attacker, victim, dam, SKILL_SMASH, 0);
        generate_smash_dismount_messages(attacker, victim);
        rots::combat::issue_command(rots::combat::combat_command::dismount, victim, mutable_arg(""), 0, 0, 0);
        if (number() >= 0.50) {
            damage(attacker, mount, dam / 2, SKILL_SMASH, 0);
        }
        return;
    }
    damage(attacker, victim, dam, SKILL_SMASH, 0);
}

int calculate_cleave_damage(char_data& attacker, int prob)
{
    int damage = get_base_skill_damage(attacker, prob);

    if (utils::get_specialization(attacker) == game_types::PS_HeavyFighting) {
        damage += 5;
    }

    return damage;
}

int calculate_stomp_damage(char_data& attacker, int prob)
{
    return get_base_skill_damage(attacker, prob) / 2;
}

void apply_stomp_affect(char_data* attacker, char_data* victim)
{
    if (!rots::entity::dispatch_target_valid(attacker, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            attacker);
        return;
    }

    int prob = get_prob_skill(attacker, victim, SKILL_STOMP);

    if (prob < 0) {
        damage(attacker, victim, 0, SKILL_STOMP, 0);
        return;
    }
    int wait_delay = number(8, PULSE_VIOLENCE);
    apply_victim_delay(victim, wait_delay);
    damage(attacker, victim, calculate_stomp_damage(*attacker, prob), SKILL_STOMP, 0);
}

void apply_overrun_damage(char_data* attacker, char_data* victim)
{
    if (!rots::entity::dispatch_target_valid(attacker, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            attacker);
        return;
    }
    int prob = get_prob_skill(attacker, victim, SKILL_OVERRUN);

    if (prob < 0) {
        damage(attacker, victim, 0, SKILL_OVERRUN, 0);
        return;
    }

    int wait_delay = PULSE_VIOLENCE;
    apply_victim_delay(victim, wait_delay);
    damage(attacker, victim, calculate_overrun_damage(*attacker, prob), SKILL_OVERRUN, 0);
}

void apply_cleave_damage(char_data* attacker, char_data* victim)
{
    if (!rots::entity::dispatch_target_valid(attacker, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            attacker);
        return;
    }

    int prob = get_prob_skill(attacker, victim, SKILL_CLEAVE);

    if (prob < 0) {
        damage(attacker, victim, 0, SKILL_CLEAVE, 0);
        return;
    }

    damage(attacker, victim, calculate_cleave_damage(*attacker, prob), SKILL_CLEAVE, 0);
}

// buf retirement (combat-trio wave, Task 1; combat-trio-census.md section
// 5.8 -- 4 of the file's 10 genuine `buf` sites). LOCAL-COMPOSITION only,
// same precedent as generate_smash_dismount_messages() above; std::format
// already returns a std::string, so the strcpy/.c_str() round-trip through
// the global scratch buffer is no longer needed. Output strings
// byte-identical.
void generate_frenzy_message(char_data* character)
{
    std::string message = std::format("You enter a frenzied state, filling your body with "
                                       "overwhelming power!\r\n");
    act(message, FALSE, character, NULL, NULL, TO_CHAR);
    message = "$n enters a frenzied state, making $s strikes grow with fervor!\r\n";
    act(message, TRUE, character, 0, NULL, TO_ROOM);
}

void apply_frenzy_affect(char_data* character)
{
    // generate messages
    generate_frenzy_message(character);
    // create affect
    struct affected_type af;
    af.type = SKILL_FRENZY;
    af.duration = 20;
    af.modifier = 20;
    af.location = APPLY_NONE;
    af.bitvector = 0;
    // apply affect
    affect_to_char(character, &af);
    SET_TACTICS(character, TACTICS_BERSERK);
}

void room_target(char_data* ch, void (*skill_damage)(char_data* character, char_data* victim))
{
    char_data* victim = nullptr;
    char_data* nxt_victim = nullptr;
    auto mount = ch->mount_data.mount;
    for (victim = room_of(ch)->people; victim; victim = nxt_victim) {
        nxt_victim = victim->next_in_room; // LS1-ALLOW: save-next (body extracts current node via skill_damage function-pointer callback)
        if (victim != ch && mount != victim) {
            skill_damage(ch, victim);
        }
    }
}
} // namespace olog_hai

ACMD(do_cleave)
{
    // arg retirement (combat-trio wave, Task 1; combat-trio-census.md
    // section 5.8 -- 1 of the file's 5 genuine `arg` sites). LOCAL-
    // COMPOSITION only: a local char array replaces the shared global
    // `arg` scratch buffer that one_argument()'s output param wrote into
    // and this function otherwise discarded -- same size as the retired
    // global (db.h:375's char arg[MAX_STRING_LENGTH]) to preserve
    // behavior exactly.
    char first_arg[MAX_STRING_LENGTH];
    one_argument(argument, first_arg);

    if (!olog_hai::is_skill_valid(ch, SKILL_CLEAVE)) {
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();

    if (!timer.is_skill_allowed(*ch, SKILL_CLEAVE)) {
        send_to_char("You can't use this skill yet.\r\n", ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);
    send_to_char("You arch back and swing your weapon with great velocity!\n\r", ch);
    olog_hai::room_target(ch, &olog_hai::apply_cleave_damage);
    timer.add_skill_timer(*ch, SKILL_CLEAVE, CLEAVE_TIMER);
    // Stun the Olog-hai after using the skill
    int wait_delay = number(8, PULSE_VIOLENCE);
    WAIT_STATE_FULL(ch, wait_delay, 0, 0, 59, 0, 0, 0, AFF_WAITING, TARGET_NONE);
}

ACMD(do_smash)
{
    // arg retirement -- see do_cleave() above for the full rationale.
    char first_arg[MAX_STRING_LENGTH];
    one_argument(argument, first_arg);

    if (!olog_hai::is_skill_valid(ch, SKILL_SMASH)) {
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();

    if (!timer.is_skill_allowed(*ch, SKILL_SMASH)) {
        send_to_char("You can't use this skill yet.\r\n", ch);
        return;
    }

    char_data* victim = olog_hai::is_smash_target_valid(ch, wtl);

    if (victim == nullptr) {
        return;
    }

    if (!rots::entity::dispatch_target_valid(ch, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);

    int prob = olog_hai::get_prob_skill(ch, victim, SKILL_SMASH);
    timer.add_skill_timer(*ch, SKILL_SMASH, SMASH_TIMER);

    if (prob < 0) {
        damage(ch, victim, 0, SKILL_SMASH, 0);
        return;
    }

    olog_hai::apply_smash_damage(ch, victim, prob);
}

bool is_direction_valid(char_data* ch, int cmd)
{
    if (!room_of(ch)->dir_option[cmd]) {
        send_to_char("You cannot go that way.\n\r", ch);
        return false;
    } else if (room_of(ch)->dir_option[cmd]->to_room == NOWHERE) {
        send_to_char("You cannot go that way.\n\r", ch);
        return false;
    }

    if (!CAN_GO(ch, cmd)) {
        if (IS_SET(EXIT(ch, cmd)->exit_info, EX_ISHIDDEN) && !PRF_FLAGGED(ch, PRF_HOLYLIGHT)) {
            send_to_char("You cannot go that way.\n\r", ch);
            return false;
        } else if (EXIT(ch, cmd)->keyword) {
            // buf2 retirement (combat-trio wave, Task 1; combat-trio-census.md
            // section 5.8 -- all 3 of the file's genuine `buf2` sites).
            // LOCAL-COMPOSITION only: a local std::string replaces the shared
            // global `buf2` scratch buffer, read directly by send_to_char()'s
            // std::string_view parameter -- same precedent as this file's
            // `buf` retirements above. Output strings byte-identical.
            std::string door_message;
            if (IS_SHADOW(ch))
                door_message = std::format("You cannot pass through the {}.\n\r", fname(EXIT(ch, cmd)->keyword));
            else
                door_message = std::format("The {} seems to be closed.\n\r", fname(EXIT(ch, cmd)->keyword));
            send_to_char(door_message, ch);
            return false;
        } else {
            send_to_char("It seems to be closed.\n\r", ch);
            return false;
        }
    } else if (EXIT(ch, cmd)->to_room == NOWHERE) {
        send_to_char("You cannot go that way.\n\r", ch);
        return false;
    }
    return true;
}

int get_direction(std::string direction)
{
    for (auto& c : direction)
        c = toupper(c);

    if (direction == "WEST" || direction == "W") {
        return WEST;
    } else if (direction == "EAST" || direction == "E") {
        return EAST;
    } else if (direction == "NORTH" || direction == "N") {
        return NORTH;
    } else if (direction == "SOUTH" || direction == "S") {
        return SOUTH;
    } else if (direction == "UP" || direction == "U") {
        return UP;
    } else if (direction == "DOWN" || direction == "D") {
        return DOWN;
    }

    return -1;
}

ACMD(do_overrun)
{
    // arg retirement -- see do_cleave() above for the full rationale.
    // get_direction() takes its argument by value (std::string), so
    // first_arg's implicit char*->std::string conversion here is the
    // same read the retired global `arg` provided.
    char first_arg[MAX_STRING_LENGTH];
    one_argument(argument, first_arg);
    cmd = get_direction(first_arg);
    int olog_delay = PULSE_VIOLENCE;

    if (cmd == -1) {
        send_to_char("You don't know what direction that is?!\r\n", ch);
        return;
    }
    if (!olog_hai::is_skill_valid(ch, SKILL_OVERRUN)) {
        return;
    }
    if (!is_direction_valid(ch, cmd)) {
        return;
    }
    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    if (!timer.is_skill_allowed(*ch, SKILL_OVERRUN)) {
        send_to_char("You cannot use this skill yet.\r\n", ch);
        return;
    }
    int total_moves = utils::get_prof_level(PROF_WARRIOR, *ch) / 8 + number(-1, 1);
    int dis;

    // WARNING: Due to bug in overrun, we need to remove hunt aff
    // otherwise the olog-hai will hit the same target multiple times in a row.
    if (utils::is_affected_by(*ch, AFF_HUNT)) {
        REMOVE_BIT(ch->specials.affected_by, AFF_HUNT);
    }

    for (dis = 0; dis <= total_moves; dis++) {
        olog_hai::room_target(ch, &olog_hai::apply_overrun_damage);

        if (!CAN_GO(ch, cmd)) {
            break;
        }
        for (auto* tmpch : rots::entity::occupants(room_of(ch))) {
            if (tmpch->specials.fighting == ch) {
                stop_fighting(tmpch);
            }
        }

        stop_fighting(ch);
        rots::combat::issue_command(rots::combat::combat_command::move, ch, mutable_arg(""), 0, cmd + 1, 0);
    }

    if (dis < total_moves) {
        damage(ch, ch, 50, SKILL_OVERRUN, 0);
        olog_delay *= 1.5;
    } else {
        olog_hai::room_target(ch, &olog_hai::apply_overrun_damage);
    }
    WAIT_STATE_FULL(ch, olog_delay, 0, 0, 1, 0, 0, 0, AFF_WAITING, TARGET_NONE);
    timer.add_skill_timer(*ch, SKILL_OVERRUN, OVERRUN_TIMER);
}

ACMD(do_frenzy)
{
    // arg retirement -- see do_cleave() above for the full rationale.
    char first_arg[MAX_STRING_LENGTH];
    one_argument(argument, first_arg);

    if (!olog_hai::is_skill_valid(ch, SKILL_FRENZY)) {
        return;
    }

    if (affected_by_spell(ch, SKILL_FRENZY)) {
        send_to_char("You are already in a frenzy!\r\n", ch);
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    if (!timer.is_skill_allowed(*ch, SKILL_FRENZY)) {
        send_to_char("You cannot use this skill yet.\r\n", ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);
    olog_hai::apply_frenzy_affect(ch);
    timer.add_skill_timer(*ch, SKILL_FRENZY, FRENZY_TIMER);
}

ACMD(do_stomp)
{
    // arg retirement -- see do_cleave() above for the full rationale.
    char first_arg[MAX_STRING_LENGTH];
    one_argument(argument, first_arg);
    if (!olog_hai::is_skill_valid(ch, SKILL_STOMP)) {
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    if (!timer.is_skill_allowed(*ch, SKILL_STOMP)) {
        send_to_char("You cannot use this skill yet.\r\n", ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);
    send_to_char("You jump into the air and slam down feet first onto the ground!\n\r", ch);
    olog_hai::room_target(ch, &olog_hai::apply_stomp_affect);
    timer.add_skill_timer(*ch, SKILL_STOMP, STOMP_TIMER);

    // Stun the Olog-hai after using the skill
    int wait_delay = number(8, PULSE_VIOLENCE);
    WAIT_STATE_FULL(ch, wait_delay, 0, 0, 59, 0, 0, 0, AFF_WAITING, TARGET_NONE);
}
