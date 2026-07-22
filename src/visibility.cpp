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
// blocker-buster Task 4b completes the family: CAN_SEE(sub, obj, light_mode)
// (the 3-arg light/hiding overload) and its four riders
// (get_char_room_vis/get_player_vis/get_char_vis/generic_find, all
// handler.cpp) join here too. Task 4 kept them app-tier because the 3-arg
// overload's see_hiding(sub) call did not resolve in-lib (see_hiding was
// defined in ranger.cpp, a still-DEFERRED ROTS_SERVER_SOURCES TU) and
// generic_find's search_block() call was an uncensused app-tier edge
// (interpre.cpp). Task 4b's Step 1 mini-census (task-4b-report.md)
// verified both clean and cleared them: see_hiding() is carved out of
// ranger.cpp into this file (entity-pure, zero other ranger.cpp
// dependency); search_block() relocates to rots_util.cpp/rots_platform
// (platform-clean, get_number()'s precedent) -- both now resolve as
// in-lib/downward references, not upward edges.
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
#include "db.h" /* For struct index_data (mob_index[]), get_guardian_type()'s move-in dependency */
#include "entity_hooks.h" /* For dispatch_get_txt_block_from_pool(), target_from_word()'s pool dependency */
#include "handler.h"
#include "interpre.h"
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

// character_list: the global linked list of all player characters
// (entity_lifecycle.cpp, rots_entity). get_player_vis()/get_char_vis()
// below walk it, exactly as they did in handler.cpp -- same local extern
// convention that file used (handler.cpp:68) and this file already uses
// for object_list above (no shared header declares it either).
extern struct char_data* character_list;

// dirs[] (consts.cpp, L1 core data table): target_from_word()'s
// TAR_DIR_WAY branch below reads it directly, the same local
// extern-declaration convention every one of its other callers
// (act_move.cpp/mystic.cpp/ranger.cpp/etc.) already uses (no shared
// header declares it).
extern const std::string_view dirs[];

// world (db_world.cpp storage; room_data::operator[] provides the
// bounds-checked indexing target_from_word()'s TAR_OBJ_ROOM branch below
// reads through) -- same local extern-declaration convention every
// rots_combat peer (clerics.cpp/mystic.cpp/fight.cpp/etc.) already uses
// for this exact symbol (no shared header declares it).
extern struct room_data world;

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

// see_hiding() relocated from ranger.cpp (blocker-buster Task 4b; census
// section A / task-4b-brief.md Step 1(a) mini-census verdict: entity-pure --
// only GET_SKILL/GET_INT/GET_LEVEL/GET_PROF_LEVEL/GET_SPEC/GET_RACE/IS_NPC
// macros, zero other ranger.cpp dependency, no ranger-internal statics.
// Carved out of that still-DEFERRED ROTS_SERVER_SOURCES TU so the 3-arg
// CAN_SEE() overload below can call it in-lib. Declaration unchanged in
// utils.h.
/*
 * see_hiding calculates the level of GET_HIDING that
 * `seeker' should be able to see, taking into account:
 *  - the ranger level of `seeker'
 *  - the amount of awareness `seeker' has practiced
 *  - the intelligence of `seeker'
 *  - the specialization of `seeker' (stealth spec gets a bonus)
 *  - the race of `seeker' (elves get a bonus)
 */
int see_hiding(struct char_data* seeker)
{
    int can_see, awareness;

    if (IS_NPC(seeker))
        awareness = std::min(100, 40 + GET_INT(seeker) + GET_LEVEL(seeker));
    else
        awareness = GET_SKILL(seeker, SKILL_AWARENESS) + GET_INT(seeker);

    can_see = awareness * GET_PROF_LEVEL(PROF_RANGER, seeker) / 30;

    if (GET_SPEC(seeker) == PLRSPEC_STLH)
        can_see += 5;

    if (GET_RACE(seeker) == RACE_WOOD)
        can_see += 5;

    return can_see;
}

// stop_hiding() relocated verbatim from ranger.cpp:638-649
// (combat-pilot wave Task 4a; pilot-census.md section 7.9) -- same home
// as see_hiding() above (blocker-buster wave Task 4b): zero upward
// refs, zero same-file entanglement. Declaration consolidated into
// utils.h (was 7 scattered per-file local externs).
void stop_hiding(struct char_data* ch, char mode)
{
    /*
     *if mode is FALSE, then we don't send the "step" message
     */
    if (IS_SET(ch->specials.affected_by, AFF_HIDE) && mode)
        send_to_char("You step out of your cover.\r\n", ch);

    REMOVE_BIT(ch->specials.affected_by, AFF_HIDE);
    REMOVE_BIT(ch->specials2.hide_flags, HIDING_SNUCK_IN);
    GET_HIDING(ch) = 0;
}

// CAN_SEE(sub, obj, light_mode) (the 3-arg light/hiding overload) relocated
// from utility.cpp (blocker-buster Task 4b, completing Task 4's split):
// its see_hiding(sub) call now resolves in-lib (above). Its one unchecked
// world[] site -- the IS_LIGHT((obj)->in_room)/OUTSIDE(sub) macro
// expansions -- resolves via two hoisted room_by_id_total() pointers (one
// for obj's room, one for sub's room, since the two can differ for
// non-NPC targets) per the BINDING addendum's resolver-variant rule,
// mirroring CAN_SEE(sub)'s 1-arg precedent above. weather_info/act()/
// number() are the same legal peer/seam/L0 refs Task 4 already established.
/*
 * Can subject see character "obj"?  Returns 0 if sub
 * cannot see obj, and 1 if sub can see obj.  CAN_SEE
 * is called way too many times.  From testing, it's
 * called three times for a kill command.
 */
int CAN_SEE(struct char_data* sub, struct char_data* obj, int light_mode)
{
    int tmp;

    if (!obj)
        return CAN_SEE(sub);
    if ((sub) == (obj))
        return 1;
    if (PLR_FLAGGED(sub, PLR_WRITING))
        return 0;

    /*
     * If you aren't in the same room as it, and it's an NPC,
     * you can't see it, unless it's the guardian angel.
     */
    if (GET_LEVEL(sub) < LEVEL_IMMORT && sub->in_room != obj->in_room && IS_NPC(obj) && strcmp(GET_NAME(obj), "Guardian angel"))
        return 0;

    if (IS_SHADOW(obj) || IS_SHADOW(sub)) {
        if ((sub->specials.fighting == obj) || (obj->specials.fighting == sub))
            return 1;
        if (!(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)) && (GET_PERCEPTION(sub) * GET_PERCEPTION(obj) < number(1, 10000)))
            return 0;
    }

    /*
     * Light/physical dependent stuff in here: shadows can
     * see though physical objects (hence see hidden players)
     * and don't worry about light sources.
     */
    if (!(IS_SHADOW(sub))) {
        if (GET_HIDING(obj)) {
            /* mobs have a 10% chance to simply not see people that sneak in */
            if (IS_NPC(sub) && IS_SET(obj->specials2.hide_flags, HIDING_SNUCK_IN) && !number(0, 9) && !IS_NPC(obj)) {
                act("$n glances directly at you, but doesn't seem to notice "
                    "your presence.",
                    FALSE, sub, 0, obj, TO_VICT);
                return 0;
            } else
                tmp = see_hiding(sub);
            if ((tmp < GET_HIDING(obj)) && !(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT))) {
                return 0;
            }
        }

        if (!light_mode) {
            // IS_LIGHT((obj)->in_room)/OUTSIDE(sub) (utils.h) inlined against
            // resolved room pointers -- see this function's banner comment.
            // obj_room_is_dark mirrors IS_DARK(room) verbatim (on obj's
            // room); sub_room_is_outside mirrors OUTSIDE(ch) verbatim (on
            // sub's room, which can differ from obj's for non-NPC targets).
            room_data* obj_room = room_by_id_total((obj)->in_room);
            bool obj_room_is_dark = !obj_room->light && (IS_SET(obj_room->room_flags, DARK) || ((obj_room->sector_type != SECT_INSIDE && obj_room->sector_type != SECT_CITY) && (weather_info.sunlight == SUN_DARK)));
            room_data* sub_room = room_by_id_total((sub)->in_room);
            bool sub_room_is_outside = !IS_SET(sub_room->room_flags, INDOORS);

            if (obj_room_is_dark && !IS_AFFECTED((sub), AFF_INFRARED) && !(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)) && !(sub_room_is_outside && IS_AFFECTED((sub), AFF_MOONVISION) && weather_info.moonlight))
                return 0;
        }
    }
    /* End light/physical dependent stuff */

    /* If obj has an invis level, you can't see it unless you're >= that level */
    if ((GET_LEVEL(sub) < GET_INVIS_LEV(obj)) && !IS_NPC(obj))
        return 0;

    /* Blinded players don't see anything */
    if (IS_AFFECTED((sub), AFF_BLIND))
        return 0;

    /* You can't see invisible objects; unless you're an imm with HOLYLIGHT */
    if (IS_AFFECTED((obj), AFF_INVISIBLE))
        if (!(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)))
            return 0;

    /* Ok, noone can see if they are sleeping :) */
    if (GET_POS((sub)) <= POSITION_SLEEPING)
        return 0;

    return 1;
}

// get_char_room_vis()/get_player_vis()/get_char_vis() relocated from
// handler.cpp (blocker-buster Task 4b, completing Task 4's split): all
// three ride the 3-arg CAN_SEE() overload above, which now resolves
// in-lib. get_char_room_vis's one unchecked world[ch->in_room].people site
// resolves via room_by_id_total(ch->in_room)->people per the BINDING
// addendum's resolver-variant rule -- precedented by get_char_room()
// (placement.cpp) hoisting the same field the same way. get_player_vis/
// get_char_vis have no world[] touch of their own.
struct char_data* get_char_room_vis(struct char_data* ch, char* name, int dark_ok)
{
    struct char_data* i;
    int j, number, check;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    j = 1;
    // world[ch->in_room].people (original, unchecked) -> room_by_id_total
    // per the BINDING addendum's resolver-variant rule (see banner above).
    for (i = room_by_id_total(ch->in_room)->people; i && (j <= number); i = i->next_in_room) {

        check = keyword_matches_char(ch, i, tmp);
        if (check)
            if (CAN_SEE(ch, i, dark_ok)) {
                if (j == number)
                    return (i);
                j++;
            }
    }
    return (0);
}

struct char_data* get_player_vis(struct char_data* ch, char* name)
{
    struct char_data* i;

    for (i = character_list; i; i = i->next)
        if (!IS_NPC(i) && !str_cmp_nullable(i->player.name, name) && CAN_SEE(ch, i))
            return i;

    return 0;
}

struct char_data* get_char_vis(struct char_data* ch, char* name, int dark_ok)
{
    struct char_data* i;
    int j, number, check;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    /* check location */
    if ((i = get_char_room_vis(ch, name)))
        return (i);

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = character_list, j = 1; i && (j <= number); i = i->next) {
        if (other_side(ch, i))
            check = isname_nullable(tmp, pc_race_keywords[i->player.race].data());
        else
            check = isname_nullable(tmp, i->player.name);

        if (check)
            if (CAN_SEE(ch, i, dark_ok)) {
                if (j == number)
                    return (i);
                j++;
            }
    }

    return (0);
}

/* Generic Find, designed to find any object/character                    */
/* Calling :                                                              */
/*  *arg     is the sting containing the string to be searched for.       */
/*           This string doesn't have to be a single word, the routine    */
/*           extracts the next word itself.                               */
/*  bitv..   All those bits that you want to "search through".            */
/*           Bit found will be result of the function                     */
/*  *ch      This is the person that is trying to "find"                  */
/*  **tar_ch Will be NULL if no character was found, otherwise points     */
/* **tar_obj Will be NULL if no object was found, otherwise points        */
/*                                                                        */
/* The routine returns a pointer to the next word in *arg (just like the  */
/* one_argument routine).                                                 */

// generic_find() relocated from handler.cpp (blocker-buster Task 4b,
// completing Task 4's split): its search_block() call (interpre.h decl)
// now resolves in-lib via rots_util.cpp/rots_platform (Task 4b Step 1(b)
// mini-census verdict), and it rides get_char_room_vis()/get_char_vis()
// above, both now in-lib too. Its one unchecked world[ch->in_room].contents
// site resolves via room_by_id_total(ch->in_room)->contents per the BINDING
// addendum's resolver-variant rule, the same substitution get_obj_vis()
// above already uses for the identical original expression.
int generic_find(char* arg, int bitvector, struct char_data* ch,
    struct char_data** tar_ch, struct obj_data** tar_obj)
{
    static const std::string_view ignore[] = {
        "the",
        "in",
        "on",
        "at",
        "\n"
    };

    int i, namelen = 0;
    char name[256];
    char found, tmpfound;

    found = FALSE;

    /* Eliminate spaces and "ignore" words */
    while (*arg && !found) {

        for (; *arg == ' '; arg++)
            ;

        for (i = 0; (name[i] = *(arg + i)) && (name[i] != ' '); i++)
            ;
        name[i] = 0;
        namelen = i;
        arg += i;
        if (search_block(name, ignore, TRUE) > -1)
            found = TRUE;
    }

    if (!name[0])
        return (0);

    *tar_ch = 0;
    *tar_obj = 0;

    if (IS_SET(bitvector, FIND_CHAR_ROOM)) { /* Find person in room */
        if ((*tar_ch = get_char_room_vis(ch, name))) {
            return (FIND_CHAR_ROOM);
        }
    }

    if (IS_SET(bitvector, FIND_CHAR_WORLD)) {
        if ((*tar_ch = get_char_vis(ch, name))) {
            return (FIND_CHAR_WORLD);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_EQUIP)) {
        for (found = FALSE, i = 0; i < MAX_WEAR && !found; i++) {
            if (namelen > 2)
                tmpfound = (ch->equipment[i] && strn_cmp_nullable(name, ch->equipment[i]->name, namelen) == 0);
            else
                tmpfound = (ch->equipment[i] && str_cmp_nullable(name, ch->equipment[i]->name) == 0);
            if (tmpfound) {
                *tar_obj = ch->equipment[i];
                found = TRUE;
            }
        }
        if (found) {
            return (FIND_OBJ_EQUIP);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_INV)) {
        //   if ((*tar_obj = get_obj_in_list_vis(ch, name, ch->carrying,9999))) {
        if ((*tar_obj = get_obj_in_list(name, ch->carrying))) {
            return (FIND_OBJ_INV);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_ROOM)) {
        // world[ch->in_room].contents (original, unchecked) -> room_by_id_total
        // per the BINDING addendum's resolver-variant rule (get_obj_vis()
        // above already establishes this exact substitution).
        if ((*tar_obj = get_obj_in_list_vis(ch, name, room_by_id_total(ch->in_room)->contents, 9999))) {
            return (FIND_OBJ_ROOM);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_WORLD)) {
        if ((*tar_obj = get_obj_vis(ch, name))) {
            return (FIND_OBJ_WORLD);
        }
    }

    return (0);
}

// get_guardian_type() -- relocated here from utility.cpp (combat-trio wave
// Task 3; combat-trio-census.md sec.3/sec.5.5). Zero function-call edges of
// its own; reads mob_index[] (extern struct index_data* mob_index,
// db_world.cpp, L3-world) and guardian_mob[] (consts.cpp, L1-core)
// directly. The mob_index reference is what forces this function's
// destination to be L3 (rots_combat) rather than L2 (rots_entity) --
// mob_index has no L2-visible resolver seam the way world[]/room_data
// does (the four-resolver world seam covers room_by_id/room_by_id_total/
// zone_by_id/obj_index_by_id only). clerics.cpp -- already a rots_combat
// member since the combat-pilot wave -- already references mob_index via
// the identical plain-extern idiom (clerics.cpp:55), confirming this is a
// legal in-lib peer reference, not a new seam. Callers: objsave.cpp:774,
// profs.cpp:226 (both app/lib downward calls, unchanged by this move).
// Declaration unchanged: utils.h:781.
/* Returns the guardian type.  Returns INVALID_GUARDIAN if the mob is not a guardian.
 * guardian_mob is a data table defined in consts.cpp (rots_core); mob_index is the
 * mobile index table defined in db.cpp. Both are declared extern here rather than
 * pulled in via a shared header, matching the historical local-extern idiom this
 * function used before its relocation out of consts.cpp. */
int get_guardian_type(int race_number, const char_data* in_guardian_mob)
{
    extern struct index_data* mob_index;
    extern int guardian_mob[MAX_RACES][3];
    if (race_number >= MAX_RACES)
        return INVALID_GUARDIAN;

    int virtual_number = mob_index[in_guardian_mob->nr].virt;
    for (int guardian_type = AGGRESSIVE_GUARDIAN; guardian_type <= MYSTIC_GUARDIAN;
         ++guardian_type) {
        if (guardian_mob[race_number][guardian_type] == virtual_number) {
            return guardian_type;
        }
    }

    return INVALID_GUARDIAN;
}

// ---------------------------------------------------------------------------
// report_wrong_target()/target_from_word() relocated verbatim from
// interpre.cpp (spell-family closure wave Task 1; sf-census.md section
// 4.1). Both are target-argument parse/presentation helpers spell_pa.cpp's
// is_target_valid()-adjacent call sites need once it promotes -- the same
// domain as this file's own get_char_room_vis()/get_char_vis()/
// generic_find() target-resolution family, just moved verbatim rather than
// rewritten. interpre.cpp keeps calling them via its own existing (local,
// header-less) declarations -- unaffected by this move, now a legal
// app->lib downward call instead of a same-TU reference.
// ---------------------------------------------------------------------------

void report_wrong_target(struct char_data* ch, int mask, char has_arg)
{
    if (IS_SET(mask, TAR_TEXT_ALL)) {
        send_to_char("Strange. Please report to gods what you just did (1).\n\r",
            ch);
        return;
    }

    /* first, some argument exists. */
    if (has_arg) {
        if (IS_SET(mask, TAR_TEXT)) {
            send_to_char("Strange. You need a better argument for this.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_ALL)) {
            send_to_char("You need to do this to \"all\"\n\r", ch);
            return;
        }
        /* recognizing gold - lame, but who cares */

        if (IS_SET(mask, TAR_CHAR_WORLD)) {
            send_to_char("Nobody by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_CHAR_ROOM)) {
            send_to_char("Nobody here by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_FIGHT_VICT)) {
            send_to_char("You need to fight somebody for this.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_ROOM)) {
            send_to_char("Nothing here by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_INV)) {
            send_to_char("You don't have that.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_EQUIP)) {
            send_to_char("You are not wearing that.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_WORLD)) {
            send_to_char("There is nothing by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_GOLD)) {
            send_to_char("You can do that with money only.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_DIR_NAME)) {
            send_to_char("Nothing here by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_DIR_WAY)) {
            send_to_char("What direction is that?\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_SELF_ONLY)) {
            send_to_char("You can do it to yourself only.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_IGNORE)) {
            send_to_char("Strange. Please report to gods what you just did (2).\n\r",
                ch);
            return;
        }
    } else { /* no argument */
        if (IS_SET(mask, TAR_NONE_OK)) {
            send_to_char("Strange. Please report to gods what you just did (3).\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_TEXT)) {
            send_to_char("You need some argument here.\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_FIGHT_VICT)) {
            send_to_char("Your victim is not here!\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_SELF) || IS_SET(mask, TAR_SELF_ONLY)) {
            send_to_char("You can do that to self only.\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_IGNORE)) {
            send_to_char("You need no argument here.\n\r", ch);
            return;
        }
    }
    send_to_char("You can not do it this way.\n\r", ch);
    return;
}

char* target_from_word(struct char_data* ch, char* argument, int mask, struct target_data* t1)
/*
 * This one tries to take a target from argument string.
 * Possible targets are determined from the mask argument.
 * Returns the target in t1 and the remaining string as return value
 */
{
    int tmp, arg_i, tmpvalue;
    char word[MAX_INPUT_LENGTH];
    struct char_data* tmpch;
    struct obj_data* tmpobj;

    /***************************************************
    Okay, here we parse the argument line for two targets, if they are
    there. Priority is, from the highest.
    For no argument:
    TAR_NONE_OK, TARGET_IGNORE
    TAR_FIGHT_VICT
    TAR_SELF, TARGET_SELF_ONLY

    For some argument:
    TAR_TEXT_ALL - sends the whole line as a text argument, as for narrate.
    TAR_TEXT
    TARGET_ALL
    TAR_GOLD
    TAR_CHAR_ROOM
    TAR_CHAR_WORLD
    TAR_OBJ_ROOM
    TAR_OBJ_INV
    TAR_OBJ_EQUIP
    TAR_OBJ_WORLD
    TAR_DIRECTION (NAME, then WAY)
    TAR_VALUE
    **************************************************/

    arg_i = 0;
    while (argument[arg_i] && (argument[arg_i] <= ' '))
        arg_i++;
    t1->ptr.other = 0;
    t1->type = TARGET_NONE;
    t1->choice = TAR_IGNORE;
    t1->ch_num = 0;

    if (IS_SET(mask, TAR_TEXT_ALL)) {
        // get_from_txt_block_pool() (comm.cpp's real app-tier body) inlined
        // to rots::entity::dispatch_get_txt_block_from_pool() -- the one
        // documented deviation from strict byte-for-byte text this move
        // makes (spell-family closure wave Task 1): comm.cpp's plain
        // no-arg getter is not itself inverted, only reachable through
        // entity_hooks.h's existing hook (world-seed Task 2), the same
        // seam entity_lifecycle.cpp's target_data::operator=() already
        // uses for this exact call.
        t1->ptr.text = rots::entity::dispatch_get_txt_block_from_pool();
        strcpy(GET_TARGET_TEXT(t1), argument + arg_i);
        t1->type = TARGET_TEXT;
        t1->choice = TAR_TEXT_ALL;
        return argument + strlen(argument);
    }

    if (IS_SET(mask, TAR_IGNORE))
        return argument + arg_i;

    if (!argument[arg_i]) {
        if (IS_SET(mask, TAR_NONE_OK))
            return argument + arg_i;
        else if (IS_SET(mask, TAR_FIGHT_VICT) && (ch->specials.fighting)) {
            t1->ptr.ch = ch->specials.fighting;
            t1->ch_num = ch->specials.fighting->abs_number;
            t1->type = TARGET_CHAR;
            t1->choice = TAR_FIGHT_VICT;
            return argument + arg_i;
        } else if (IS_SET(mask, TAR_SELF) || IS_SET(mask, TAR_SELF_ONLY)) {
            t1->ptr.ch = ch;
            t1->ch_num = ch->abs_number;
            t1->type = TARGET_CHAR;
            t1->choice = TAR_SELF;
            return argument + arg_i;
        }
        if (IS_SET(mask, TAR_IGNORE))
            return argument;

        return 0;
    }

    /* some argument exists. parsing the first argument. */
    tmpvalue = 0;
    tmp = arg_i;
    while (isdigit(argument[arg_i])) {
        tmpvalue = tmpvalue * 10 + argument[arg_i] - (int)('0');
        arg_i++;
    }
    if (argument[arg_i] == '.') {
        arg_i = tmp;
        tmpvalue = 0;
    }

    if (argument[arg_i] == '\'') {
        for (tmp = 0, arg_i++; argument[arg_i] && (argument[arg_i] != '\'');
            tmp++, arg_i++)
            word[tmp] = argument[arg_i];
        word[tmp] = 0;

        if (argument[arg_i] == '\'')
            arg_i++;
    } else {
        for (tmp = 0; argument[arg_i] && (argument[arg_i] > ' ');
            tmp++, arg_i++)
            word[tmp] = argument[arg_i];
        word[tmp] = 0;
    }

    if (IS_SET(mask, TAR_TEXT)) {
        t1->ptr.text = rots::entity::dispatch_get_txt_block_from_pool();
        strcpy(GET_TARGET_TEXT(t1), word);
        t1->type = TARGET_TEXT;
        t1->choice = TAR_TEXT;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_ALL) && !strcmp(word, "all")) {
        t1->type = TARGET_ALL;
        t1->ch_num = 0;
        t1->choice = TAR_ALL;
        return argument + arg_i;
    }
    /* recognizing gold. lame */
    if (IS_SET(mask, TAR_GOLD) && tmpvalue && !strcmp(word, "gold")) {
        t1->type = TARGET_GOLD;
        t1->ch_num = tmpvalue * COPP_IN_GOLD;
        t1->choice = TAR_GOLD;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_GOLD) && tmpvalue && !strcmp(word, "silver")) {
        t1->type = TARGET_GOLD;
        t1->ch_num = tmpvalue * COPP_IN_SILV;
        t1->choice = TAR_GOLD;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_GOLD) && tmpvalue && !strcmp(word, "copper")) {
        t1->type = TARGET_GOLD;
        t1->ch_num = tmpvalue;
        t1->choice = TAR_GOLD;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_CHAR_ROOM)) {
        tmpch = get_char_room_vis(ch, word, (IS_SET(mask, TAR_DARK_OK)) ? 1 : 0);
        if (tmpch) {
            t1->ptr.ch = tmpch;
            t1->ch_num = tmpch->abs_number;
            t1->type = TARGET_CHAR;
            t1->choice = TAR_CHAR_ROOM;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_CHAR_WORLD)) {
        tmpch = get_char_vis(ch, word, (IS_SET(mask, TAR_DARK_OK)) ? 1 : 0);
        if (tmpch) {
            t1->ptr.ch = tmpch;
            t1->ch_num = tmpch->abs_number;
            t1->choice = TAR_CHAR_WORLD;
            t1->type = TARGET_CHAR;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_OBJ_ROOM)) {
        tmpobj = get_obj_in_list(word, world[ch->in_room].contents);
        if (tmpobj) {
            t1->ptr.obj = tmpobj;
            t1->ch_num = 0;
            t1->type = TARGET_OBJ;
            t1->choice = TAR_OBJ_ROOM;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_OBJ_INV)) {
        tmpobj = get_obj_in_list(word, ch->carrying);
        if (tmpobj) {
            t1->ptr.obj = tmpobj;
            t1->ch_num = 0;
            t1->type = TARGET_OBJ;
            t1->choice = TAR_OBJ_INV;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_OBJ_EQUIP)) {
        for (tmp = 0; tmp < MAX_WEAR; tmp++)
            if (ch->equipment[tmp] && isname_nullable(word, ch->equipment[tmp]->name)) {
                t1->ptr.obj = ch->equipment[tmp];
                t1->ch_num = 0;
                t1->type = TARGET_OBJ;
                t1->choice = TAR_OBJ_EQUIP;
                return argument + arg_i;
            }
    }
    if (IS_SET(mask, TAR_OBJ_WORLD)) {
        tmpobj = get_obj(word);
        if (tmpobj) {
            t1->ptr.obj = tmpobj;
            t1->ch_num = 0;
            t1->type = TARGET_OBJ;
            t1->choice = TAR_OBJ_WORLD;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_DIR_NAME)) {
        for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
            if (EXIT(ch, tmp) && !str_cmp_nullable(EXIT(ch, tmp)->keyword, word)) {
                t1->ch_num = tmp;
                t1->type = TARGET_DIR;
                t1->choice = TAR_DIR_NAME;
                return argument + arg_i;
            }
    }
    if (IS_SET(mask, TAR_DIR_WAY)) {
        for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
            if (!strncmp(dirs[tmp].data(), word, strlen(word))) {
                t1->ch_num = tmp;
                t1->type = TARGET_DIR;
                t1->choice = TAR_DIR_WAY;
                return argument + arg_i;
            }
    }
    if (IS_SET(mask, TAR_VALUE) && (isdigit(*word) || (*word == '-'))) {
        tmp = atoi(word);
        t1->ch_num = tmp;
        t1->type = TARGET_VALUE;
        t1->choice = TAR_VALUE;
        return argument + arg_i;
    }

    /* wrong target */
    return 0;
}
