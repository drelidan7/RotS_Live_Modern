// visibility.cpp -- the CAN_SEE/CAN_SEE_OBJ visibility family, relocated
// from utility.cpp (blocker-buster wave Task 4, census section A:
// .superpowers/sdd/blocker-census.md). Zero L2-entity and zero L3-world
// callers of CAN_SEE exist anywhere in the tree (census section B's
// decisive caller-tier scan) -- every caller is a combat-row peer
// (fight/mobact/mystic/olog_hai/ranger/spec_pro) or app (act_*/comm/
// interpre/mudlle/shop/objsave/handler/utility), both legal downward or
// peer edges once visibility lives here. `weather_info` (weather.cpp,
// rots_world/L3-world) is referenced as a LEGAL PEER REFERENCE -- plain
// extern, no seam -- per the parent spec's L3-peer rule (spec sec3/sec10
// step4), the same precedent rots_world's own peer link to rots_persist
// established for its one sanctioned cross-L3 edge (see the
// ROTS_COMBAT_SOURCES/rots_combat CMakeLists.txt comments for the
// RotS::world PUBLIC link this file requires).
//
// get_real_OB()/get_real_parry() move here too: their
// player_spec::weapon_master_handler dependency (warrior_spec_handlers.h)
// is a rots_combat seed TU (weapon_master_handler.cpp, already in
// ROTS_COMBAT_SOURCES since combat-seed Task 1) -- an intra-lib peer
// reference now, not the app edge that kept them STAY-APP in utility.cpp
// through placement-seam Task 5. Their former STAY-APP comments are
// retired here; the OB/parry/dodge trio (get_real_dodge() already lives in
// char_utils_combat.cpp/rots_entity, placement-seam Task 5) reunites
// across rots_entity + rots_combat -- see AGENTS.md's Task 5 follow-up
// (blocker-buster Task 5 updates the Dead/Unused-Code trio paragraph).
//
// CAN_SEE(sub, obj, light_mode) -- the 3-arg light/hiding overload -- does
// NOT move here: see the STAY-APP comment above its definition in
// utility.cpp for the verified reason (its see_hiding() call does not
// resolve in-lib). get_char_room_vis/get_player_vis/get_char_vis/
// generic_find (handler.cpp) likewise stay app for the same cascading
// reason plus (generic_find only) an independent search_block() app edge
// -- see their own STAY comments in handler.cpp and task-4-report.md.
//
// world[] access: CAN_SEE(sub)'s IS_LIGHT/OUTSIDE macro expansions
// (utils.h) index world[room] with NO bounds check anywhere in the
// original body -- per the BINDING addendum's resolver-variant rule this
// resolves via a single room_by_id_total((sub)->in_room) hoisted at entry,
// with the macros' logic inlined against the resolved pointer (mirroring
// CAN_GO()/can_breathe()'s precedent in environment_utils.cpp). See
// task-4-report.md for the exact before/after quote.

#include "char_utils.h"
#include "comm.h"
#include "handler.h"
#include "spells.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"

// object_list: the global linked list of all objects in the world
// (entity_lifecycle.cpp, rots_entity). get_obj_vis() below walks it as a
// last-resort scan, exactly as it did in handler.cpp -- same local extern
// convention that file used (handler.cpp:67), not header-declared.
extern struct obj_data* object_list;

// max_race_str[] (consts.cpp, L1 core data table): GET_BAL_STR (utils.h)
// expands to a direct max_race_str[GET_RACE(ch)] read, used by get_real_OB()/
// get_real_parry() below. Same local extern-declaration pattern
// equipment.cpp/handler.cpp use for their own max_race_str reference (no
// shared header declares it).
extern int max_race_str[];

/*
 * Can character see at all?
 */
int CAN_SEE(struct char_data* sub)
{
    if ((sub)->in_room == NOWHERE)
        return 0;
    if (IS_AFFECTED((sub), AFF_BLIND) || PLR_FLAGGED(sub, PLR_WRITING))
        return 0;

    if (IS_SHADOW(sub))
        return 1;

    // IS_LIGHT((sub)->in_room)/OUTSIDE(sub) (utils.h) inlined against a
    // resolved room pointer -- see the file banner's resolver-variant
    // note. room_is_dark mirrors IS_DARK(room) verbatim; room_is_outside
    // mirrors OUTSIDE(ch) verbatim.
    room_data* r = room_by_id_total((sub)->in_room);
    bool room_is_dark = !r->light && (IS_SET(r->room_flags, DARK) || ((r->sector_type != SECT_INSIDE && r->sector_type != SECT_CITY) && (weather_info.sunlight == SUN_DARK)));
    bool room_is_outside = !IS_SET(r->room_flags, INDOORS);

    if (room_is_dark && (!IS_AFFECTED((sub), AFF_INFRARED) && !PRF_FLAGGED((sub), PRF_HOLYLIGHT) && !(room_is_outside && IS_AFFECTED((sub), AFF_MOONVISION) && weather_info.moonlight)))
        return 0;

    return 1;
}

int CAN_SEE_OBJ(char_data* sub, obj_data* obj)
{
    if (!sub || !obj)
        return 0;

    if (IS_SHADOW(sub)) {
        if (IS_SET((obj)->obj_flags.extra_flags, ITEM_MAGIC) || IS_SET((obj)->obj_flags.extra_flags, ITEM_WILLPOWER))
            return 1;
        else
            return 0;
    }

    if ((!IS_SET((obj)->obj_flags.extra_flags, ITEM_INVISIBLE) || IS_AFFECTED((sub), AFF_DETECT_INVISIBLE)) && CAN_SEE(sub))
        return 1;

    return 0;
}

// STAY-APP trio comments retired (blocker-buster Task 4): get_real_OB()'s
// and get_real_parry()'s weapon_master_handler dependency now resolves
// in-lib -- see the file banner above.
int get_real_OB(char_data* ch)
{
    if (IS_NPC(ch)) {
        int base_npc_ob = (GET_OB(ch) + GET_BAL_STR(ch) + 15 - utils::get_skill_penalty(*ch) + GET_LEVEL(ch) / 2);
        if (IS_AFFECTED(ch, AFF_CONFUSE)) {
            base_npc_ob -= (get_confuse_modifier(ch) * 2 / 3);
        }
        return base_npc_ob;
    }

    int sun_mod = 0;
    int tmpob, tactics = 0, weapon_skill = 0;

    obj_data* weapon = ch->equipment[WIELD];

    int warrior_level = GET_PROF_LEVEL(PROF_WARRIOR, ch);
    int max_warrior_level = GET_MAX_RACE_PROF_LEVEL(PROF_WARRIOR, ch);
    int offense_stat = GET_BAL_STR(ch);

    // Light fighters can use dex and some of their ranger level with light weapons.
    if (utils::get_specialization(*ch) == game_types::PS_LightFighting) {
        if (weapon) {
            int bulk = weapon->get_bulk();
            if (bulk <= 2 || (bulk == 3 && weapon->get_weight() <= LIGHT_WEAPON_WEIGHT_CUTOFF)) {
                offense_stat = std::max(offense_stat, int(ch->tmpabilities.dex));

                int ranger_bonus = GET_PROF_LEVEL(PROF_RANGER, ch) / 3;
                warrior_level += ranger_bonus;
            }
        }
    }

    int ob_bonus = (warrior_level * 3 + 3 * max_warrior_level * GET_LEVELA(ch) / 30) / 2 + offense_stat;

    tmpob = GET_OB(ch);
    tmpob -= utils::get_skill_penalty(*ch);

    player_spec::weapon_master_handler weapon_master(ch);
    tmpob += weapon_master.get_bonus_OB();

    if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) == 0) {
        return tmpob + ob_bonus;
    } else if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) > 0) {
        weapon_skill = utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK);
        tmpob -= (GET_STR(ch) / 2 - 6);
    } else {
        weapon_skill = utils::get_raw_knowledge(*ch, weapon_skill_num(weapon->get_weapon_type()));

        if (IS_TWOHANDED(ch)) {
            if (weapon->is_ranged_weapon()) {
                tmpob += weapon->obj_flags.value[2] * (200 + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 100 - 15;
                weapon_skill = (weapon_skill + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 2;
            } else {
                tmpob += weapon->obj_flags.value[2] * (200 + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 100 - 15;
                weapon_skill = (weapon_skill + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 2;
            }
        } else {
            tmpob -= (weapon->obj_flags.value[2] * 2 - 6);
        }
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        tmpob += ob_bonus - ob_bonus / 4 - 8;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpob += ob_bonus - ob_bonus / 8 - 4;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpob += ob_bonus;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpob += ob_bonus + ob_bonus / 16 + 2;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpob += ob_bonus + ob_bonus / 16 + 5 + GET_RAW_SKILL(ch, SKILL_BERSERK) / 8;
        tactics = 10;
        break;
    default:
        tmpob += ob_bonus + GET_BAL_STR(ch);
        break;
    };

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        tmpob -= (get_confuse_modifier(ch) * 2 / 3);

    /* to get the pre-power of arda malus, substitute 10 for sun_mod */
    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            tmpob = tmpob * 3 / 4 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            tmpob = tmpob * 4 / 5 - sun_mod;
    }

    if (!CAN_SEE(ch))
        tmpob -= 10;

    if (!weapon)
        tmpob += weapon_skill * (GET_STR(ch) + 20) * tactics / 1000;
    else
        tmpob += weapon_skill * (weapon->obj_flags.value[2] + 20) * tactics / 1000;

    return tmpob;
}

int get_real_parry(struct char_data* ch)
{
    int sun_mod = 0;

    if (IS_NPC(ch)) {
        if (IS_AFFECTED(ch, AFF_CONFUSE))
            return (GET_PARRY(ch) + GET_LEVEL(ch) / 2 + 15) - (get_confuse_modifier(ch) * 2 / 3);
        else
            return (GET_PARRY(ch) + GET_LEVEL(ch) / 2 + 15);
    }

    int tmpparry, tmpskill, tactics, bonus, weapon_bonus = 0;
    struct obj_data* weapon;

    tmpparry = GET_PARRY(ch);
    bonus = GET_PROF_LEVEL(PROF_WARRIOR, ch) * 2 + std::min(30, GET_LEVEL(ch)) + GET_BAL_STR(ch);

    player_spec::weapon_master_handler weapon_master(ch);
    tmpparry += weapon_master.get_bonus_PB();

    weapon = ch->equipment[WIELD];
    if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) == 0) {
        return tmpparry + bonus / 2;
    } else if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) > 0) {
        tmpskill = GET_RAW_SKILL(ch, SKILL_NATURAL_ATTACK);
    } else {
        weapon_bonus = weapon->obj_flags.value[1];

        tmpskill = GET_RAW_KNOWLEDGE(ch, weapon_skill_num(weapon->obj_flags.value[3]));
        if (isname_nullable("bow", weapon->name)) {
            tmpskill = GET_RAW_SKILL(ch, SKILL_ARCHERY);
        }

        if (IS_TWOHANDED(ch)) {
            tmpskill = (tmpskill + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 2;
            if (isname_nullable("bow", weapon->name)) {
                tmpskill = (tmpskill + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 2;
            }
        }
    }

    tmpskill = (tmpskill + 3 * GET_RAW_KNOWLEDGE(ch, SKILL_PARRY)) / 4;
    if (GET_TACTICS(ch) == TACTICS_BERSERK) {
        tmpskill /= 2;
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        tmpparry += bonus / 2 + 3 * bonus / 16;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpparry += bonus / 2 + bonus / 8;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpparry += bonus / 2;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 12;
        break;
    default:
        tmpparry += bonus / 2;
        tactics = 10;
        break;
    };

    tmpparry += tmpskill * (weapon_bonus + 20) * (14 - tactics) / 1000;
    // Parry should now have bigger effect on two-handers:
    if (IS_AFFECTED(ch, AFF_TWOHANDED))
        tmpparry += weapon_bonus / 2;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        tmpparry -= (get_confuse_modifier(ch) * 2 / 3);

    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            tmpparry = tmpparry * 8 / 9 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
    }

    if (!CAN_SEE(ch)) {
        tmpparry -= 10;
    }

    return tmpparry;
}

struct obj_data* get_obj_in_list_vis(struct char_data* ch, std::string_view name,
    struct obj_data* list, int num)
{
    if (num < 9999) {
        obj_data* indexed_object = list;
        for (int list_index = 1; indexed_object != nullptr && list_index < num; ++list_index) {
            indexed_object = indexed_object->next_content;
        }
        return indexed_object;
    }

    const auto [requested_match_number, query] = parse_numbered_name(name);
    if (requested_match_number == 0) {
        return (0);
    }

    int match_index = 1;
    for (obj_data* candidate_object = list;
         candidate_object != nullptr && match_index <= requested_match_number;
         candidate_object = candidate_object->next_content) {
        if (candidate_object->name != nullptr && isname(query, candidate_object->name, 0)
            && CAN_SEE_OBJ(ch, candidate_object)) {
            if (match_index == requested_match_number) {
                return candidate_object;
            }
            ++match_index;
        }
    }
    return (0);
}

/*search the entire world for an object, and return a pointer  */
struct obj_data* get_obj_vis(struct char_data* ch, char* name)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    /* scan items carried */
    if ((i = get_obj_in_list_vis(ch, name, ch->carrying, 9999)))
        return (i);

    /* scan room */
    // world[ch->in_room].contents (original, unchecked) -> room_by_id_total
    // per the BINDING addendum's resolver-variant rule (file banner above).
    if ((i = get_obj_in_list_vis(ch, name, room_by_id_total(ch->in_room)->contents, 9999)))
        return (i);

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    /* ok.. no luck yet. scan the entire obj list   */
    for (i = object_list, j = 1; i && (j <= number); i = i->next)
        if (isname_nullable(tmp, i->name, 0))
            if (CAN_SEE_OBJ(ch, i)) {
                if (j == number)
                    return (i);
                j++;
            }
    return (0);
}

struct obj_data* get_object_in_equip_vis(struct char_data* ch,
    char* arg, struct obj_data* equipment[], int* j)
{

    for ((*j) = 0; (*j) < MAX_WEAR; (*j)++)
        if (equipment[(*j)])
            if (CAN_SEE_OBJ(ch, equipment[(*j)]))
                if (isname_nullable(arg, equipment[(*j)]->name))
                    return (equipment[(*j)]);

    return (0);
}
