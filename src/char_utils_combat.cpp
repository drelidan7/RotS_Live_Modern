#include "char_utils_combat.h"
#include "char_utils.h"
#include "environment_utils.h"
#include "object_utils.h"
#include "utils.h"

#include "handler.h" // for affect_to_char and affected_by_spell
#include "spells.h"

#include "entity_hooks.h"
#include "rots/core/character.h"
#include "rots/core/object.h" // obj_data methods -- get_bow_weapon_damage()/get_weapon_damage()/weight_coof()/armor_absorb() (placement-seam Task 5)
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

//============================================================================
// do_squareroot()/get_bow_weapon_damage()/get_weapon_damage()/weight_coof()/
// armor_absorb()/get_real_stealth()/get_real_dodge()/can_sense()/
// get_power_of_arda()/check_resistances() relocated verbatim from
// utility.cpp (placement-seam Task 5; plan
// docs/superpowers/plans/2026-07-19-placement-seam.md; census
// .superpowers/sdd/placement-census.md's utility.cpp table). All ten are
// entity-pure combat-stat helpers (char_data/obj_data field and macro
// arithmetic, no output/combat-call dependency); get_real_stealth() is the
// one resolver-dependent mover in this batch -- its single unchecked
// world[ch->in_room].sector_type read (no bounds test in the original)
// becomes room_by_id_total(ch->in_room)->sector_type per the BINDING
// addendum's resolver-variant rule (see task-5-report.md for the exact
// before/after quote). do_squareroot() moves together with its sole caller
// get_weapon_damage(); get_power_of_arda() moves alongside its callers
// get_real_dodge() (this batch) and get_real_OB()/get_real_parry() (now in
// visibility.cpp/rots_combat, blocker-buster Task 4 -- a legal L3->L2
// downward edge; see those functions' own comments there). Declarations
// unchanged (utils.h, except do_squareroot()/get_bow_weapon_damage()/
// weight_coof()/get_real_stealth(), which have no declaring header
// anywhere in the tree -- file-local by precedent). get_followers_level()
// is NOT in this file -- it's defined in char_utils.cpp (placement-seam
// Task 5 review Minor: this banner previously mis-attributed it here via a
// comment copy-paste slip).
//============================================================================

inline int
do_squareroot(int i, struct char_data*)
{
    if (i / 4 > 170)
        i = 170 * 4;

    return ((4 - i % 4) * square_root[i / 4] + (i % 4) * square_root[i / 4 + 1]);
}

//============================================================================
int get_bow_weapon_damage(const obj_data& weapon)
{
    const char_data* owner = weapon.get_owner();
    int level_factor = 0;
    if (owner) {
        level_factor = std::min(weapon.get_level(), owner->get_level()) / 3;
    } else {
        level_factor = weapon.get_level() / 3;
    }
    return level_factor + weapon.get_ob_coef() + weapon.get_bulk();
}

//============================================================================
int get_weapon_damage(struct obj_data* obj)
{
    int parry_coef, OB_coef, dam_coef;
    int tmp, bulk, str_speed, null_speed, ene_regen;
    int obj_level, skill_type;
    struct char_data* owner;

    if (GET_ITEM_TYPE(obj) != ITEM_WEAPON) {
        mudlog("Calculating damage for non-weapon!", NRM, LEVEL_IMMORT, TRUE);
        return 1;
    }

    game_types::weapon_type w_type = obj->get_weapon_type();
    if (w_type == game_types::WT_BOW || w_type == game_types::WT_CROSSBOW) {
        return get_bow_weapon_damage(*obj);
    }

    parry_coef = obj->obj_flags.value[1];
    OB_coef = obj->obj_flags.value[0];
    bulk = obj->obj_flags.value[2];
    skill_type = weapon_skill_num(obj->obj_flags.value[3]);
    obj_level = obj->obj_flags.level;
    owner = obj->carried_by;

    /*
     * If the weapon is owned by someone (i.e., we aren't just statting
     * an object), then that person will affect how well the weapon
     * works.  Currently, there are two maluses:
     *   (1) If the wielder's level is a good deal lower than the
     *       object's level, then the level of the object is lowered
     *       to be closer to the wielder's; thus reducing damage.
     *   (2) If the wielder has low weapon skill for the object, then
     *       we reduce the damage.
     */
    if (owner != NULL) {
        /* Case (1) */
        if (obj_level > (GET_LEVEL(owner) * 4 / 3 + 7))
            obj_level -= (obj_level - GET_LEVEL(owner) * 4 / 3 - 7) * 2 / 3;

        /* Case (2): for skill=100, use full obj_level; skill=0, use obj_level/2 */
        obj_level = obj_level * GET_SKILL(owner, skill_type) / 100;
    }

    switch (obj->obj_flags.value[3]) {
    case 2: /* whip */
        parry_coef += 8;
        OB_coef -= 5;
        break;

    case 3:
    case 4:
        parry_coef -= 2;
        break;

    case 6:
    case 7:
        parry_coef += 3;
        break;
    }

    if (parry_coef < -7)
        parry_coef = parry_coef / 3 - 1; /* i.e. parry < -7 */
    else if (parry_coef < 0)
        parry_coef = parry_coef / 2;

    if (parry_coef > 5)
        parry_coef = parry_coef * 2 - 5;

    if (OB_coef < -7)
        OB_coef = OB_coef / 2 - 1;
    else if (OB_coef < 0)
        OB_coef = OB_coef * 2 / 3;

    if (OB_coef > 40)
        OB_coef = 40; /* just against crashes */

    /* dam_coef is about 3000 on avg. low weapon */
    dam_coef = (40 + obj_level - parry_coef) * (50 - OB_coef) * 4 / 3;
    dam_coef = dam_coef * (20 - abs(bulk - 3)) / 20;

    null_speed = 225;
    if (GET_OBJ_WEIGHT(obj) == 0)
        GET_OBJ_WEIGHT(obj) = 1;

    /* all equal damage in 2-hands, with 20 str 20 dex. 100% attack */
    str_speed = 2 * 20 * 2500000 / (GET_OBJ_WEIGHT(obj) * (bulk + 3));

    tmp = (1000000 / (1000000 / str_speed + 1000000 / (null_speed * null_speed)));

    /* ene_regen is about 100 on average */
    ene_regen = do_squareroot(tmp / 100, NULL) / 20;
    dam_coef = dam_coef / ene_regen * 3;

    if (dam_coef > 70)
        dam_coef = 70 + (dam_coef - 70) * 3 / 4;

    if (dam_coef > 90)
        dam_coef = 90 + (dam_coef - 90) * 3 / 4;

    return dam_coef; /* returning damage * 10 */
}

int weight_coof(struct obj_data* obj)
{
    if (CAN_WEAR(obj, ITEM_WEAR_BODY))
        return 3;
    if (CAN_WEAR(obj, ITEM_WEAR_ARMS))
        return 2;
    if (CAN_WEAR(obj, ITEM_WEAR_LEGS))
        return 2;

    return 1;
}

int armor_absorb(struct obj_data* obj)
{
    int absorb, points, encumb_points;

    if (obj->obj_flags.value[0] == -1)
        return 0;

    encumb_points = obj->obj_flags.value[2] * 6 + GET_OBJ_WEIGHT(obj) / weight_coof(obj) / 20;

    points = obj->obj_flags.level + encumb_points;

    /* bonus of 15 abs. at 30 abs., double at 0 */
    if (encumb_points < 30)
        points += encumb_points * (60 - encumb_points) / 90;

    if (obj->obj_flags.value[2]) /* encumb for low encumb */
        points += 3;

    absorb = points - obj->obj_flags.value[1] * 9;
    if (absorb < 0)
        absorb = 0;

    if (absorb > 50)
        absorb = 100 - 2500 / absorb;

    return (absorb);
}

/*
 * get_real_stealth has sort of turned into the default
 * "how stealthy is this person?" function.  It now takes
 * into account specialization and sneak in addition to
 * stealth skill, race, sector type and encumbrance.
 */
int get_real_stealth(struct char_data* ch)
{
    int percent;

    /* This is the only bonus we give for the stealth skill */
    percent = GET_RAW_SKILL(ch, SKILL_STEALTH) / 4;

    /* This is the only place where stealth spec helps hiders */
    if (GET_SPEC(ch) == PLRSPEC_STLH)
        percent += 5;

    /* Now, sneaking helps stealth */
    if (IS_AFFECTED(ch, AFF_SNEAK) && IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN)) {
        if (IS_NPC(ch))
            percent += std::max(100, 10 * GET_LEVEL(ch) / 30);
        else
            percent += GET_SKILL(ch, SKILL_SNEAK) / 20;
    }

    room_data* r = room_by_id_total(ch->in_room);
    switch (r->sector_type) {
    case SECT_INSIDE:
        percent -= 20;
        break;
    case SECT_CITY:
        percent -= 10;
        break;
    case SECT_FIELD:
        percent += 0;
        break;
    case SECT_FOREST:
        percent += 15;
        break;
    case SECT_HILLS:
        percent += 5;
        break;
    case SECT_MOUNTAIN:
        percent += 0;
        break;
    case SECT_WATER_SWIM:
        percent -= 20;
        break;
    case SECT_WATER_NOSWIM:
        percent -= 20;
        break;
    case SECT_UNDERWATER:
        percent -= 20;
        break;
    case SECT_ROAD:
        percent -= 5;
        break;
    case SECT_CRACK:
        percent -= 10;
        break;
    case SECT_DENSE_FOREST:
        percent += 20;
        break;
    case SECT_SWAMP:
        percent += 5;
        break;
    }

    if (GET_RACE(ch) == RACE_DWARF)
        percent -= 10;

    if (GET_RACE(ch) == RACE_HOBBIT || GET_RACE(ch) == RACE_BEORNING || GET_RACE(ch) == RACE_HARADRIM)
        percent += 5;

    percent -= utils::get_leg_encumbrance(*ch);
    percent -= utils::get_encumbrance(*ch) / 4;

    return (percent);
}

int get_real_dodge(struct char_data* ch)
{
    int sun_mod = 0;

    if (IS_NPC(ch)) {
        if (IS_AFFECTED(ch, AFF_CONFUSE))
            return (GET_DODGE(ch) + GET_DEX(ch) - 5 + GET_LEVEL(ch) / 2) - (get_confuse_modifier(ch) * 2 / 3);
        else
            return (GET_DODGE(ch) + GET_DEX(ch) - 5 + GET_LEVEL(ch) / 2);
    }

    int dodge = ((GET_SKILL(ch, SKILL_DODGE) + GET_SKILL(ch, SKILL_STEALTH) / 2 + 60) * GET_PROF_LEVEL(PROF_RANGER, ch) / 200 + (GET_SKILL(ch, SKILL_DODGE) + GET_SKILL(ch, SKILL_STEALTH) / 4) / 20);
    dodge -= utils::get_dodge_penalty(*ch);
    dodge += 3;

    if (GET_RACE(ch) == RACE_BEORNING) {
        dodge += 20;
    }

    if (GET_TACTICS(ch) == TACTICS_BERSERK)
        dodge /= 2;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        dodge -= (get_confuse_modifier(ch) * 2 / 3);

    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            dodge = dodge * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            dodge = dodge * 8 / 9 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            dodge = dodge * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            dodge = dodge * 9 / 10 - sun_mod;
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        return (dodge + GET_DODGE(ch) + 6) + GET_DEX(ch);
    case TACTICS_CAREFUL:
        return (dodge + GET_DODGE(ch) + 4) + GET_DEX(ch);
    case TACTICS_NORMAL:
        return (dodge + GET_DODGE(ch)) + GET_DEX(ch);
    case TACTICS_AGGRESSIVE:
        return (dodge + GET_DODGE(ch) - 4) + GET_DEX(ch);
    case TACTICS_BERSERK:
        return (dodge + GET_DODGE(ch) - 4) + GET_DEX(ch) / 2;
    default:
        return (dodge + GET_DODGE(ch) + GET_DEX(ch));
    };
}

/*
 * If a character is affected by the detect hidden spell,
 * can they sense a hidden character?  Returns 1 if sub
 * can see obj, returns 0 otherwise.
 */
int can_sense(char_data* sub, char_data* obj)
{
    if (!IS_AFFECTED((sub), AFF_DETECT_HIDDEN) || GET_PERCEPTION(obj) <= 0)
        return 0;

    if (30 + (GET_PROF_LEVEL(PROF_CLERIC, sub) * 3) > (100 - GET_PERCEPTION(obj) * GET_PERCEPTION(sub) / 100 + GET_HIDING(obj) / 4))
        return 1;
    else
        return 0;
}

int get_power_of_arda(struct char_data* ch)
{
    struct affected_type* aff;
    int sun_mod;

    aff = affected_by_spell(ch, SPELL_ARDA);
    if (aff)
        sun_mod = aff->modifier / 25;
    else
        sun_mod = 0;

    return sun_mod;
}

int check_resistances(char_data* victim, int attack_type)
{
    extern skill_data skills[];

    if ((attack_type < MAX_SKILLS) && IS_RESISTANT(victim, skills[attack_type].skill_spec))
        return 1;

    if ((attack_type < MAX_SKILLS) && IS_VULNERABLE(victim, skills[attack_type].skill_spec))
        return -1;

    if (((attack_type >= TYPE_HIT) && (attack_type <= TYPE_CRUSH)) || attack_type == SKILL_ARCHERY) {
        if (IS_RESISTANT(victim, PLRSPEC_WILD))
            return 1;

        if (IS_VULNERABLE(victim, PLRSPEC_WILD))
            return -1;
    }

    return 0;
}

// saves_power() relocated verbatim from spell_pa.cpp:137-149
// (combat-pilot wave Task 4a; pilot-census.md section 7.1) -- entity-pure
// willpower-vs-casting-power roll, alongside this file's other combat-stat
// family (get_real_dodge/check_resistances/armor_absorb/get_weapon_damage).
// Declaration: no shared header ever declared this symbol -- its sole
// caller, clerics.cpp, keeps its own local declaration unchanged.
char saves_power(const char_data* victim, sh_int casting_power, sh_int save_bonus)
{
    sh_int victim_save_bonus = victim->points.willpower + save_bonus;

    int saving_throw_roll = number(0, victim_save_bonus * victim_save_bonus);
    int saving_throw_dc = number(0, casting_power * casting_power);

    if (saving_throw_roll > saving_throw_dc) {
        return 1;
    } else {
        return 0;
    }
}
