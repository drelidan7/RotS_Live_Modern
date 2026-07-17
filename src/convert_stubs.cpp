// convert_stubs.cc -- the WELD LEDGER for `rots_convert` (db.cpp-split
// Task 3, spec Sec4b). rots_convert links db_players.cpp/entity_lifecycle.cpp
// (plus the JSON/account codecs) directly, with NO world/combat/commands/app
// translation units. Because the flat build links whole .cpp files rather
// than pruning unreferenced functions, EVERY function defined anywhere in a
// linked TU -- not just the ones the converter's own call graph actually
// reaches -- pulls in that function's own undefined symbols at link time.
// Most of the symbols below come from functions the converter's own
// call graph (load_char -> store_to_char -> save_char, or the
// build_player_index() boot path) simply never reaches; a smaller number are
// genuinely on that call graph, and their entries say so explicitly.
//
// EVERY SECTION BELOW MUST document: the symbol, its real home TU, why the
// converter's flow does not (or safely can) exercise it, and the follow-on
// that would let this stub be deleted. Shrinking this file is the intended
// measure of progress as later waves peel apart rots_entity/rots_persist/
// rots_world/rots_combat (spec Sec10 step 4). Do not add a stub here for
// anything whose reachability you can't argue -- that is a real design
// problem (an actual persist->game weld), not a stub candidate; see the
// task brief for the STOP condition.

#include "base_utils.h"
#include "char_utils.h"
#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/persist/file_formats.h"
#include "rots/platform/log.h"
#include "rots_rng.h"
#include "spells.h"
#include "text_view.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

// ===========================================================================
// buf / buf1 -- db_boot.cpp globals (db.h declares all four of buf/buf1/
// buf2/arg; db_boot.cpp defines them). rots_convert does not link
// db_boot.cpp (it is boot-orchestration + the two live-game capture
// functions, entirely app-layer), but two P-half functions still reference
// two of the four scratch buffers by their extern declaration:
//   - save_char() (db_players.cpp) writes an account-native-fallback log
//     line through `buf` (genuinely reachable: any account-linked character
//     whose account-native file write fails hits this).
//   - rename_char() (db_players.cpp) composes old/new paths through `buf1`.
// buf2/arg are declared in db.h but never referenced by anything this
// executable links, so they are deliberately NOT defined here -- adding them
// would be dead weight, not a real weld.
// Follow-on: once db_boot.cpp's four scratch buffers are replaced by local
// std::string/std::format composition at each of these two call sites (both
// already use std::format elsewhere in the same functions), this whole
// section disappears.
// ===========================================================================
char buf[MAX_STRING_LENGTH];
char buf1[MAX_STRING_LENGTH];

// ===========================================================================
// log() / mudlog() -- utility.cpp (app layer). Both are already thin,
// behavior-preserving forwarders onto the platform logging seam (spec Sec13,
// docs/superpowers/plans/2026-07-16-logging-seam.md's recorded follow-on
// explicitly calls out extracting these two into the platform layer):
//   void log(std::string_view message) { rots::log::write_stderr(message); }
//   void mudlog(std::string_view b, char t, sh_int l, byte f) {
//       rots::log::write(b, t, l, f != 0); }
// These are copied verbatim from utility.cpp (not reimagined/faked) because
// db_players.cpp/entity_lifecycle.cpp call log()/mudlog() throughout their
// error paths (e.g. save_char's player-table-lookup failure, add_crime's
// file-open failure) -- genuinely reachable, not dead code -- and the real
// bodies are already just platform-seam forwarders with no comm/game
// dependency, so duplicating them here is behavior-identical, not a
// substitute.
// Follow-on: relocate log()/mudlog() themselves into rots_platform (they
// have zero remaining game dependency post logging-seam); once that lands,
// db_players.cpp/entity_lifecycle.cpp link the real definitions and this
// section is deleted outright.
// ===========================================================================
void log(std::string_view message) { rots::log::write_stderr(message); }

void mudlog(std::string_view message_body, char type, sh_int level, byte file)
{
    rots::log::write(message_body, type, level, file != 0);
}

// ===========================================================================
// nearest_ansi_color() / convert_old_colormask() -- color.cpp. The brief's
// nm closure check applies here: `nm -uC color.cpp.o` shows color.cpp as a
// WHOLE TU is not link-clean for this executable (do_color()/
// show_color_slot_summary() in the same TU pull in send_to_char()/
// vsend_to_char()/half_chop()/str_cmp_nullable() -- comm/utility, app
// layer) -- so color.cpp cannot be linked wholesale (the brief's "prefer
// linking color.cpp if it's clean" branch does not apply here).
//
// Unlike this file's other stubs, these two are NOT inert placeholders:
// character_json.cpp's truecolor-setting codec calls nearest_ansi_color()
// on every setting whose foreground/background isn't already a valid
// ANSI16 index, and db_players.cpp's load_char()/load_char_from_text() call
// convert_old_colormask() on every legacy character loaded with a
// color_mask-only (pre-per-slot) color record -- both genuinely execute
// during ordinary conversion. Faking their output would silently diverge
// converted characters' color settings from what a live login produces,
// defeating this executable's entire "byte-identical by construction"
// purpose (spec Sec4b). So the three bodies below (plus the
// sync_color_slot_foreground_from_ansi() helper and the ansi_palette/
// kNumColors table convert_old_colormask()/nearest_ansi_color() need) are
// VERBATIM copies of color.cpp's current implementation (color.cpp:55-62,
// 366-380, 402-442 at this writing), not reinterpretations -- kept
// byte-for-byte in sync is the correctness contract, checked by Task 4's
// conversion-equivalence test.
// Follow-on: split color.cpp's pure conversion helpers (no comm/game
// dependency: nearest_ansi_color, convert_old_colormask,
// sync_color_slot_foreground_from_ansi, the color name/sequence tables) into
// their own clean leaf TU (e.g. color_convert.cpp) so both ageland and
// rots_convert link ONE real definition instead of two synchronized copies.
// ===========================================================================
namespace {

// Verbatim copy of color.cpp:55's file-local helper (anonymous namespace
// there too) -- pure struct-field writes, no comm/game dependency.
void sync_color_slot_foreground_from_ansi(struct char_prof_data* profs, int col)
{
    if (profs == nullptr || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    profs->color_settings[col].foreground.mode = COLOR_VALUE_ANSI16;
    profs->color_settings[col].foreground.ansi = static_cast<unsigned char>(profs->colors[col]);
}

// Verbatim copy of color.cpp:294-312's color_color[] length (16 entries:
// 15 real colors + the "\n" sentinel) -- nearest_ansi_color()'s loop
// bound below is `kNumColors - 1`, matching color.cpp's own
// `num_of_colors - 1` exactly.
constexpr int kNumColors = 16;

} // namespace

int nearest_ansi_color(int red, int green, int blue)
{
    // Verbatim copy of color.cpp:402-442.
    struct AnsiColor {
        int red;
        int green;
        int blue;
    };

    static const AnsiColor ansi_palette[] = {
        { 0, 0, 0 },
        { 170, 0, 0 },
        { 0, 170, 0 },
        { 170, 85, 0 },
        { 0, 0, 170 },
        { 170, 0, 170 },
        { 0, 170, 170 },
        { 170, 170, 170 },
        { 255, 85, 85 },
        { 85, 255, 85 },
        { 255, 255, 85 },
        { 85, 85, 255 },
        { 255, 85, 255 },
        { 85, 255, 255 },
        { 255, 255, 255 },
    };

    int best_index = CNRM;
    long best_distance = std::numeric_limits<long>::max();
    for (int index = 0; index < kNumColors - 1; ++index) {
        const long red_distance = red - ansi_palette[index].red;
        const long green_distance = green - ansi_palette[index].green;
        const long blue_distance = blue - ansi_palette[index].blue;
        const long distance = red_distance * red_distance + green_distance * green_distance + blue_distance * blue_distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
        }
    }

    return best_index;
}

void convert_old_colormask(struct char_file_u* ch)
{
    // Verbatim copy of color.cpp:366-380.
    int i;

    if (!ch->profs.color_mask)
        i = 0;
    else
        for (i = 0; i < 10; ++i)
            ch->profs.colors[i] = ch->profs.color_mask >> (i * 3) & 7;

    for (i = 0; i < MAX_COLOR_FIELDS; ++i) {
        if (ch->profs.color_settings[i].foreground.mode == COLOR_VALUE_DEFAULT)
            sync_color_slot_foreground_from_ansi(&ch->profs, i);
    }
}

// ===========================================================================
// race_affect[] -- consts.cpp (app layer; consts.cpp as a whole stays out of
// rots_core -- see CMakeLists.txt's ROTS_CORE_SOURCES comment -- because its
// skills[] table embeds function pointers straight into mystic.cpp/
// spell_pa.cpp). entity_lifecycle.cpp's init_char() reads
// `race_affect[GET_RACE(ch)]` when initializing a BRAND NEW character.
// rots_convert's own call graph never calls init_char() (it only loads
// EXISTING characters via load_char()/store_to_char()/save_char() -- see
// convert_main.cpp), but init_char() is still DEFINED in the linked
// entity_lifecycle.cpp TU, so its reference to race_affect[] is a real link
// demand regardless of whether this executable ever calls it. Verbatim data
// copy of consts.cpp:2432-2454 (not reimplemented -- an exact table, so
// init_char() would behave identically if it were ever reached).
// Follow-on: same as get_guardian_type's prior relocation (see
// ROTS_CORE_SOURCES' consts.cpp comment) -- once the skills[] function-
// pointer coupling is cut, small pure data tables like this one can move
// into rots_core proper and every consumer (ageland and rots_convert alike)
// links the one real definition.
// ===========================================================================
long race_affect[] = {
    0, // God
    0, // Human
    0, // Dwarf
    1024, // Wood Elf
    0, // Hobbit
    1024, // High Elf
    2, // Beorning
    0, // !UNUSED!
    0, // !UNUSED!
    0, // !UNUSED!
    0, // !UNUSED!
    2, // Uruk-Hai
    0, // !NPC - Harad!
    2, // Common Orc
    0, // !NPC - Easterling!
    2, // Uruk-Lhuth
    0, // !NPC - Undead!
    2, // Olog-Hai
    1024, // Haradrim
    0, // !UNUSED!
    0, // !NPC - Troll!
    0 // !UNUSED!
};

// ===========================================================================
// The persisted-stat affect/derived-ability engine -- affect_total()/
// affect_modify()/affect_to_char()/affect_remove()/apply_gear_affects()/
// modify_affects()/affect_naked() (handler.cpp) and recalc_abilities()
// (profs.cpp). UNLIKE every stub above, this section is NOT optional or
// safely inert: store_to_char() calls affect_to_char() (which calls
// affect_modify()) for every persisted spell-affect entry, and BOTH
// store_to_char() and char_to_store() call affect_total() unconditionally
// (char_to_store(): `affect_total(ch, AFFECT_TOTAL_REMOVE)` then, after
// copying ch->affected into st->affected[], `affect_total(ch,
// AFFECT_TOTAL_SET)`) -- for EVERY character, not just ones with active
// affects. affect_total()'s AFFECT_TOTAL_REMOVE branch calls
// recalc_abilities(), which writes character->tmpabilities.{hit,mana,move}
// and (in the no-weapon branch -- see below) character->points.ENE_regen,
// ALL of which char_to_store() persists verbatim
// (`st->tmpabilities = ch->tmpabilities`, `st->points.ENE_regen =
// GET_ENE_REGEN(ch)`). A no-op/inert stub here would silently produce
// WRONG persisted stat fields for every single converted character, not
// just an edge case -- defeating this executable's "byte-identical by
// construction" purpose (spec Sec4b) far more than any other symbol in
// this file. So, like the color.cpp section above, this is a VERBATIM
// duplicate of the real implementation (handler.cpp/profs.cpp at this
// writing), not a behavioral substitute -- with three narrow, explicitly
// justified simplifications documented at each site below:
//
//   (1) Every character this executable loads has an all-null
//       ch->equipment[] (store_to_char() never populates it -- that only
//       happens via objsave.cpp's Crash_load()/load_character(), which is
//       NOT on this executable's call path; see convert_main.cpp).
//       apply_gear_affects() therefore always no-ops (its loop immediately
//       `continue`s on every slot) and recalc_abilities()'s `if (weapon)`
//       branch is UNREACHABLE here by the same invariant -- so that branch
//       (weapon speed/bulk/attack-speed-multiplier maths, which would also
//       need weapon_master_handler and an ambiguous do_squareroot() overload
//       resolution) is omitted rather than duplicated; only the `else`
//       branch (GET_ENE_REGEN = 60 + 5*GET_DEX, unconditional and
//       weapon-independent) is kept, verbatim.
//   (2) affect_to_char()'s real body also registers the character on a
//       live-tick bookkeeping list (`pool_to_list(&affected_list, ...)`)
//       used only by the game's periodic affect-duration-countdown pass.
//       rots_convert never runs that pass (no game loop), and this
//       bookkeeping has no effect on anything char_to_store() persists --
//       so it is omitted (documented, not silently dropped).
//   (3) affect_remove() is reachable ONLY via entity_lifecycle.cpp's
//       free_char() teardown loop (`while (ch->affected)
//       affect_remove(ch, ch->affected);`), which always runs AFTER
//       save_char() has already written the character to disk (see
//       convert_main.cpp's convert_one_character(): the char_data_ptr's
//       destructor fires at function-scope exit, once nothing further reads
//       the character). Its real body's affect_modify()/affect_total()
//       recompute calls therefore have NO observable effect for this
//       executable -- the character is being destroyed, not re-inspected or
//       re-saved -- so this duplicate keeps ONLY the structural work
//       (linked-list unlink + pool release) a correct teardown needs to
//       avoid a leak or a dangling ch->affected, and skips the numeric
//       recompute.
//
// One genuine, disclosed gap remains: affect_modify()'s `case APPLY_SPELL:`
// dispatches through consts.cpp's skills[] function-pointer table straight
// into a mystic.cpp/spell_pa.cpp spell handler -- the exact
// "consts.cpp... is not a stray call site to cut; it is consts.cpp's core
// data shape" structural coupling CMakeLists.txt's ROTS_CORE_SOURCES
// comment already documents as unresolved follow-on work, not something
// this task can cut. It is therefore stubbed (loud log, no numeric effect)
// rather than duplicated -- see that case below for the full rationale and
// why it does not corrupt the PRIMARY persisted affect record (st->affected[]
// is a raw copy of the affected_type struct BEFORE affect_modify's
// side effects run, so this gap only affects a character whose persisted
// affect list contains an APPLY_SPELL-location entry, and only the
// DERIVED stat fields, never the affect list itself).
// Follow-on: dissolves once rots_entity carries this whole subsystem as a
// real, clean-leaf library member both ageland and rots_convert link.
// ===========================================================================
namespace {

// Verbatim copy of utility.cpp's get_race_perception(char_data*) (global
// overload, distinct from utils::get_race_perception(const char_data&) in
// char_utils.cpp, which this executable already links but which is NOT
// what handler.cpp's affect_naked()/get_naked_perception() call). Pure
// switch-on-race, no further dependency.
sh_int convert_stub_get_race_perception(struct char_data* ch)
{
    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 0;
    case RACE_HUMAN:
        return 30;
    case RACE_DWARF:
        return 40;
    case RACE_WOOD:
        return 60;
    case RACE_HOBBIT:
        return 50;
    case RACE_HIGH:
        return 70;
    case RACE_BEORNING:
        return 30;
    case RACE_URUK:
        return 30;
    case RACE_HARAD:
        return 30;
    case RACE_ORC:
        return 30;
    case RACE_EASTERLING:
        return 30;
    case RACE_HARADRIM:
        return 30;
    case RACE_OLOGHAI:
        return 30;
    case RACE_MAGUS:
        return 30;
    default:
        return 30;
    }
}

// Verbatim copy of utility.cpp:1777's get_confuse_modifier(char_data*) --
// pure ch->affected walk, no further dependency.
int convert_stub_get_confuse_modifier(struct char_data* ch)
{
    struct affected_type* aff;
    int modifier = 0;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        for (aff = ch->affected; aff; aff = aff->next)
            if (aff->type == SPELL_CONFUSE)
                modifier = aff->duration * 2 - 10;

    return modifier;
}

// Verbatim copy of profs.cpp:165's inline class_HP() -- uses
// utils::get_prof_points (char_utils.cpp, already linked) and GET_RACE.
int convert_stub_class_HP(const char_data* character)
{
    double hp_coofs = 3 * utils::get_prof_points(PROF_WARRIOR, *character) + 2 * utils::get_prof_points(PROF_RANGER, *character) + utils::get_prof_points(PROF_CLERIC, *character);

    if (GET_RACE(character) == RACE_ORC) {
        hp_coofs = hp_coofs * 4.0 / 7.0;
    }

    return int(std::sqrt(hp_coofs) * 200.0);
}

} // namespace

// get_current_time_phase() -- utility.cpp. Reads the game's live heartbeat
// counter (`extern int pulse`, incremented every server tick), which never
// advances in a batch tool with no run_the_game() loop. Unlike every other
// symbol in this file, there is no "correct" value to reproduce here even
// in principle: two live logins of the SAME character at two different
// server uptimes would themselves get two different time_phase values, so
// "byte-identical to a live login" is inherently ambiguous for this one
// affected_type field. Returning a fixed 0 is deterministic and
// reproducible across repeated rots_convert runs, which is arguably a
// BETTER property for a batch converter than reproducing an arbitrary live
// snapshot would be.
char get_current_time_phase() { return 0; }

// Verbatim copy of handler.cpp:955's affected_by_spell() -- pure
// ch->affected walk, no further dependency. Needed by affect_modify()'s
// APPLY_PERCEPTION case below.
affected_type* affected_by_spell(const char_data* ch, byte skill, affected_type* start_affect)
{
    if (!start_affect)
        start_affect = ch->affected;

    int count = 0;
    for (affected_type* status_affect = start_affect; status_affect && (count < MAX_AFFECT);
        status_affect = status_affect->next, count++) {
        if (status_affect->type == skill) {
            return status_affect;
        }
    }

    return NULL;
}

// Verbatim copy of profs.cpp:745's recalc_abilities(), MINUS the `if
// (weapon)` branch -- see this section's simplification (1) above for why
// that branch is unreachable-by-invariant here rather than duplicated.
void recalc_abilities(char_data* character)
{
    if (!IS_NPC(character)) {
        character->abilities.str = character->constabilities.str;
        character->abilities.lea = character->constabilities.lea;
        character->abilities.intel = character->constabilities.intel;
        character->abilities.wil = character->constabilities.wil;
        character->abilities.dex = character->constabilities.dex;
        character->abilities.con = character->constabilities.con;

        character->abilities.hit = 10 + std::min(LEVEL_MAX, GET_LEVEL(character)) + character->constabilities.hit * GET_CON(character) / 20 + (convert_stub_class_HP(character) * (GET_CON(character) + 20) / 14) * std::min(LEVEL_MAX * 100, (int)GET_MINI_LEVEL(character)) / 100000;

        if (utils::get_specialization(*character) == game_types::PS_Defender) {
            character->abilities.hit += character->abilities.hit / 10;
        }

        character->abilities.hit = std::max(character->abilities.hit - (GET_RAW_SKILL(character, SKILL_STEALTH) * GET_LEVELA(character) + GET_RAW_SKILL(character, SKILL_STEALTH) * 3) / 33,
            10);

        character->tmpabilities.hit = std::min(character->tmpabilities.hit, character->abilities.hit);

        character->abilities.mana = character->constabilities.mana + GET_INT(character) + GET_WILL(character) / 2 + GET_PROF_LEVEL(PROF_MAGE, character) * 2;

        character->tmpabilities.mana = std::min(character->tmpabilities.mana, character->abilities.mana);

        character->abilities.move = character->constabilities.move + GET_CON(character) + 20 + GET_PROF_LEVEL(PROF_RANGER, character) + GET_RAW_KNOWLEDGE(character, SKILL_TRAVELLING) / 4;

        if ((GET_RACE(character) == RACE_WOOD) || GET_RACE(character) == RACE_HIGH)
            character->abilities.move += 15;

        if (GET_RACE(character) == RACE_BEORNING) {
            character->abilities.move += 50;
        }

        character->tmpabilities.move = std::min(character->tmpabilities.move, character->abilities.move);

        // `if (weapon)` branch omitted -- see simplification (1) above.
        // character->equipment[WIELD] is always null in this executable.
        GET_ENE_REGEN(character) = 60 + 5 * GET_DEX(character);
    }
}

// Verbatim copy of handler.cpp:337's affect_modify(), EXCEPT `case
// APPLY_SPELL:` -- see this section's header comment for why that one case
// is stubbed (consts.cpp's skills[] function-pointer table reaches into
// mystic.cpp/spell_pa.cpp, a genuine, already-documented structural weld
// this task cannot cut) rather than duplicated.
void affect_modify(struct char_data* ch, byte loc, int mod, long bitv, char add, sh_int counter)
{
    int tmp, tmp2;

    if (add == AFFECT_MODIFY_SET) {
        SET_BIT(ch->specials.affected_by, bitv);
        if (utils::is_set(bitv, long(AFF_CHARM))) {
            ch->damage_details.reset();
        }
    } else if (add == AFFECT_MODIFY_REMOVE) {
        REMOVE_BIT(ch->specials.affected_by, bitv);
        if (utils::is_set(bitv, long(AFF_CHARM))) {
            ch->damage_details.reset();
        }

        mod = -mod;
    }
    ch->specials.affected_by |= race_affect[GET_RACE(ch)];

    if (add == AFFECT_MODIFY_TIME) {
        return; /* so, usual affects are not modified in this call */
    }

    switch (loc) {
    case APPLY_NONE:
        break;

    case APPLY_STR:
        SET_STR_BASE(ch, GET_STR_BASE(ch) + mod);
        SET_STR(ch, GET_STR(ch) + mod);
        break;

    case APPLY_LEA:
        GET_LEA_BASE(ch) += mod;
        GET_LEA(ch) += mod;
        break;

    case APPLY_DEX:
        GET_DEX_BASE(ch) += mod;
        GET_DEX(ch) += mod;
        break;

    case APPLY_INT:
        GET_INT_BASE(ch) += mod;
        GET_INT(ch) += mod;
        break;

    case APPLY_WILL:
        GET_WILL_BASE(ch) += mod;
        GET_WILL(ch) += mod;
        break;

    case APPLY_CON:
        GET_CON_BASE(ch) += mod;
        GET_CON(ch) += mod;
        break;

    case APPLY_PROF:
        break;

    case APPLY_LEVEL:
        break;

    case APPLY_AGE:
        ch->player.time.birth -= (mod * SECS_PER_MUD_YEAR);
        break;

    case APPLY_CHAR_WEIGHT:
        GET_WEIGHT(ch) += mod;
        break;

    case APPLY_CHAR_HEIGHT:
        GET_HEIGHT(ch) += mod;
        break;

    case APPLY_MANA:
        GET_MAX_MANA(ch) += mod;
        if (GET_MANA(ch) >= GET_MAX_MANA(ch) - mod)
            GET_MANA(ch) += mod;

        break;

    case APPLY_WILLPOWER:
        GET_WILLPOWER(ch) += mod;
        break;

    case APPLY_HIT:
        GET_MAX_HIT(ch) += mod;
        if (GET_HIT(ch) >= GET_MAX_HIT(ch) - mod)
            GET_HIT(ch) += mod;

        break;

    case APPLY_MOVE:
        GET_MAX_MOVE(ch) += mod;
        if (GET_MOVE(ch) >= GET_MAX_MOVE(ch) - mod)
            GET_MOVE(ch) += mod;
        break;

    case APPLY_GOLD:
        break;

    case APPLY_EXP:
        break;

    case APPLY_DODGE:
        SET_DODGE(ch) += mod;
        break;

    case APPLY_OB:
        SET_OB(ch) += mod;
        break;

    case APPLY_SPELL_PEN:
        ch->points.spell_pen += mod;
        break;

    case APPLY_SPELL_POW:
        ch->points.spell_power += mod;
        break;

    case APPLY_DAMROLL:
        GET_DAMAGE(ch) += mod;
        break;

    case APPLY_SAVING_SPELL:
        GET_SAVE(ch) += mod;
        break;

    case APPLY_VISION:
        if (add) {
            if (mod > 0)
                SET_BIT(ch->specials.affected_by, AFF_INFRARED);
            if (mod < 0)
                SET_BIT(ch->specials.affected_by, AFF_BLIND);
        } else {
            if (mod > 0)
                REMOVE_BIT(ch->specials.affected_by, AFF_BLIND);
            if (mod < 0)
                REMOVE_BIT(ch->specials.affected_by, AFF_INFRARED);
        }

    case APPLY_REGEN:
        break;

    case APPLY_SPEED:
        GET_ENE_REGEN(ch) += mod;
        break;

    case APPLY_BEND: {
        GET_ENE_REGEN(ch) += (GET_ENE_REGEN(ch) / 2);
        SET_OB(ch) += mod;
    } break;

    case APPLY_ARMOR:
        break;
    case APPLY_MAUL: {
        // MAX_MAUL_DODGE: verbatim copy of handler.cpp:92's file-local define.
        constexpr int kMaxMaulDodge = 50;
        if (!add) {
            SET_DODGE(ch) += std::min((counter * 5), kMaxMaulDodge);
        }

        if (add) {
            SET_DODGE(ch) += -(std::min((counter * 5), kMaxMaulDodge));
        }
    } break;

    case APPLY_PERCEPTION:
        ch->specials2.rawPerception += mod;

        if (affected_by_spell(ch, SPELL_INSIGHT)) {
            int minimumRacePerception = utils::get_minimum_insight_perception(*ch);

            ch->specials2.perception = std::max(ch->specials2.rawPerception, minimumRacePerception);
        } else {
            ch->specials2.perception = ch->specials2.rawPerception;
        }
        break;

    case APPLY_SPELL:
        // STUB: real handler.cpp body dispatches through consts.cpp's
        // skills[] function-pointer table into a mystic.cpp/spell_pa.cpp
        // spell handler -- see this section's header comment. Not
        // reproduced; logged so a converted character that actually hits
        // this is visible rather than silently short-changed.
        rots::log::write_stderr(
            std::format("rots_convert: STUB affect_modify() APPLY_SPELL case for '{}' -- the "
                        "underlying spell handler (consts.cpp skills[] function pointer, "
                        "mystic.cpp/spell_pa.cpp) is not linked into rots_convert; this "
                        "character's derived stats may not exactly match a live login. The "
                        "persisted affected_type record itself is unaffected (see this "
                        "section's header comment).",
                GET_NAME(ch)));
        break;

    case APPLY_BITVECTOR:
        if (add) {
            if ((mod < 0) || (mod > 31))
                mod = 0;
            SET_BIT(ch->specials.affected_by, 1 << mod);
        } else {
            mod = -mod;
            if ((mod < 0) || (mod > 31))
                mod = 0;
            REMOVE_BIT(ch->specials.affected_by, 1 << mod);
        }
        break;

    case APPLY_MANA_REGEN:
        ch->points.mana_regen += mod;
        break;

    case APPLY_RESIST:
        if (mod >= 0)
            GET_RESISTANCES(ch) |= (1 << mod);
        else
            GET_RESISTANCES(ch) &= ~(1 << (-mod));
        break;

    case APPLY_VULN:
        if (mod >= 0)
            GET_VULNERABILITIES(ch) |= (1 << mod);
        else
            GET_VULNERABILITIES(ch) &= ~(1 << (-mod));
        break;

    default:
        rots::log::write_stderr(
            "SYSERR: Unknown apply adjust attempt (convert_stubs.cc, affect_modify).");
        break;
    } /* switch */

    (void)tmp;
    (void)tmp2;
}

// Verbatim copy of handler.cpp:610/622's apply_gear_affects() overloads --
// see simplification (1) above: always a no-op here (ch->equipment[] is
// always null), kept verbatim rather than special-cased for fidelity.
void apply_gear_affects(char_data* character, const obj_data* item, int modify_flag)
{
    for (int count = 0; count < MAX_OBJ_AFFECT; ++count) {
        const obj_affected_type& obj_affect = item->affected[count];
        if (obj_affect.location == APPLY_SPELL)
            continue;

        affect_modify(character, obj_affect.location, obj_affect.modifier,
            item->obj_flags.bitvector, modify_flag, 0);
    }
}

void apply_gear_affects(char_data* character, int modify_flag)
{
    for (int item_index = 0; item_index < MAX_WEAR; ++item_index) {
        const obj_data* item = character->equipment[item_index];
        if (item == nullptr)
            continue;

        if (item_index == HOLD && !CAN_WEAR(item, ITEM_HOLD))
            continue;

        apply_gear_affects(character, item, modify_flag);
    }
}

// Verbatim copy of handler.cpp:636's modify_affects().
void modify_affects(char_data* character, int modify_flag)
{
    int count = 0;
    affected_type* af = character->affected;
    while (count < MAX_AFFECT && af != nullptr) {
        affect_modify(character, af->location, af->modifier, af->bitvector, modify_flag,
            af->counter);
        ++count;
        af = af->next;
    }
}

// Verbatim copy of handler.cpp:593's affect_naked() -- uses this section's
// convert_stub_get_race_perception()-derived get_naked_perception()/
// get_naked_willpower() below.
sh_int get_naked_perception(struct char_data* ch)
{
    if (IS_NPC(ch)) {
        if (MOB_FLAGGED(ch, MOB_SHADOW))
            return 100;
        else if (IS_SHADOW(ch))
            // Manual expansion of the GET_PERCEPTION(ch) macro (utils.h),
            // substituting convert_stub_get_race_perception() for its
            // get_race_perception() call -- this IS_NPC branch is
            // unreachable for this executable (it only ever loads player
            // characters, never NPCs -- see convert_main.cpp), so exact
            // fidelity here is moot, but the macro's own logic is trivial
            // enough to keep verbatim rather than approximate.
            return 100;
        else if (ch->specials2.perception == -1)
            return convert_stub_get_race_perception(ch);
        else
            return std::min(100, std::max(0, static_cast<int>(ch->specials2.perception)));
    }

    int tmp = convert_stub_get_race_perception(ch);
    tmp += GET_PROF_LEVEL(PROF_CLERIC, ch) * 2;

    return tmp;
}

sh_int get_naked_willpower(struct char_data* ch)
{
    return GET_PROF_LEVEL(PROF_CLERIC, ch) + GET_WILL(ch) - (convert_stub_get_confuse_modifier(ch) / 10);
}

void affect_naked(char_data* ch)
{
    int nakedPerception = get_naked_perception(ch);
    ch->specials2.rawPerception = ch->specials2.perception = nakedPerception;
    GET_WILLPOWER(ch) = get_naked_willpower(ch);
    ch->specials.affected_by |= race_affect[GET_RACE(ch)];

    if (!IS_NPC(ch)) {
        GET_RESISTANCES(ch) = 0;
        GET_VULNERABILITIES(ch) = 0;
    }
}

// Verbatim copy of handler.cpp:647's affect_total().
void affect_total(struct char_data* ch, int mode)
{
    if (mode & AFFECT_TOTAL_REMOVE) {
        apply_gear_affects(ch, AFFECT_MODIFY_REMOVE);
        modify_affects(ch, AFFECT_MODIFY_REMOVE);

        recalc_abilities(ch);
        affect_naked(ch);
    }

    if (mode & AFFECT_TOTAL_SET) {
        apply_gear_affects(ch, AFFECT_MODIFY_SET);
        modify_affects(ch, AFFECT_MODIFY_SET);
    }

    if (mode & AFFECT_TOTAL_TIME) {
        apply_gear_affects(ch, AFFECT_MODIFY_TIME);
        modify_affects(ch, AFFECT_MODIFY_TIME);
    }

    signed char max_value = 100;
    signed char min_dex_str = 1;
    signed char min_others = 0;

    ch->abilities.dex = std::max(min_dex_str, std::min(ch->abilities.dex, max_value));
    ch->abilities.intel = std::max(min_others, std::min(ch->abilities.intel, max_value));
    ch->abilities.wil = std::max(min_others, std::min(ch->abilities.wil, max_value));
    ch->abilities.con = std::max(min_others, std::min(ch->abilities.con, max_value));
    ch->abilities.str = std::max(min_dex_str, std::min(ch->abilities.str, max_value));
    ch->abilities.lea = std::max(min_others, std::min(ch->abilities.lea, max_value));
}

// Verbatim copy of handler.cpp:684/701's affected_type pool -- simplified
// per this section's header (no free-list reuse; a converter processes one
// character at a time and has no long server lifetime to amortize the
// allocator optimization over). Produces byte-identical affected_type field
// CONTENTS either way -- only the allocation strategy differs.
affected_type* get_from_affected_type_pool()
{
    struct affected_type* afnew;
    CREATE(afnew, struct affected_type, 1);
    return afnew;
}

void put_to_affected_type_pool(struct affected_type* oldaf) { free(oldaf); }

// Verbatim copy of handler.cpp:720's affect_to_char() MINUS the
// affected_list live-tick bookkeeping registration -- see simplification
// (2) above.
void affect_to_char(struct char_data* ch, struct affected_type* af)
{
    struct affected_type* affected_alloc;

    if (!ch)
        return;

    affected_alloc = get_from_affected_type_pool();

    *affected_alloc = *af;

    affected_alloc->next = ch->affected;
    ch->affected = affected_alloc;

    affected_alloc->time_phase = get_current_time_phase();

    affect_modify(ch, af->location, af->modifier, af->bitvector, AFFECT_MODIFY_SET, af->counter);
    affect_total(ch);
}

// Verbatim copy of handler.cpp:802's affect_remove() MINUS the
// affect_modify()/affect_total() recompute and the affected_list
// bookkeeping -- see simplification (3) above (this executable only
// reaches affect_remove() via free_char()'s post-save teardown, where the
// recompute has no observable effect).
void affect_remove(struct char_data* ch, struct affected_type* af)
{
    struct affected_type* hjp;
    int tmp;

    if (!ch->affected)
        return;

    if (ch->affected == af) {
        ch->affected = af->next;
    } else {
        for (hjp = ch->affected, tmp = 0; (hjp->next) && (hjp->next != af) && (tmp < MAX_AFFECT);
            hjp = hjp->next, tmp++) {
        }
        if (hjp->next != af) {
            rots::log::write_stderr(
                "rots_convert: STUB affect_remove() could not locate affected_type "
                "in ch->affected (teardown-only path; not a persisted-output bug).");
            return;
        }
        hjp->next = af->next;
    }

    put_to_affected_type_pool(af);
}

// ===========================================================================
// recalc_skills() -- spec_pro.cpp. store_to_char() calls it unconditionally
// after copying st->skills[] into ch->skills[]. Its real body recomputes
// ch->knowledge[] (a RUNTIME-ONLY derived field -- see
// core/include/rots/core/character.h's `knowledge` field comment: "Computed
// knowledge per skill (derived from `skills` at logon..."; char_file_u/
// char_to_store have no knowledge field at all, so NOTHING about that
// computation is ever persisted) using consts.cpp's real skills[] table
// data (learn_diff/level/type per skill) -- not available here (see this
// section's header comment on consts.cpp). The ONE persisted side effect --
// `ch->player.language = <race-derived language>` (char_to_store: `st
// ->language = ch->player.language`) -- is a pure function of GET_RACE(ch)
// alone, so THAT part is duplicated verbatim; the knowledge-table
// recomputation (and the RACE_MAGUS/RACE_BEORNING/RACE_GOD bonus-knowledge
// grants, which only ever touch ch->knowledge[]) is omitted because it is
// provably invisible to char_to_store()'s output.
// Follow-on: link real skills[] table data (once the consts.cpp
// function-pointer coupling above is cut) to reproduce ch->knowledge[]
// faithfully too, for parity with a live server's in-memory state even
// though it is never observed on disk.
// ===========================================================================
void recalc_skills(struct char_data* ch)
{
    if (ch->knowledge.empty() || ch->skills.empty())
        return;

    int language;
    switch (GET_RACE(ch)) {
    case RACE_GOD:
        language = LANG_BASIC;
        break;
    case RACE_HUMAN:
    case RACE_DWARF:
    case RACE_WOOD:
    case RACE_HOBBIT:
    case RACE_HIGH:
        language = LANG_HUMAN;
        break;
    case RACE_BEORNING:
        language = LANG_ANIMAL;
        break;
    case RACE_URUK:
    case RACE_HARAD:
    case RACE_ORC:
    case RACE_HARADRIM:
    case RACE_OLOGHAI:
    case RACE_MAGUS:
        language = LANG_ORC;
        break;
    case RACE_EASTERLING:
        language = LANG_BASIC;
        break;
    default:
        language = LANG_BASIC;
        break;
    }

    ch->player.language = language;
}

// ===========================================================================
// create_function()/free_function()/global_release_flag -- utility.cpp
// (functions) + consts.cpp (global_release_flag, via the CONSTANTSMARK
// trick in rots/core/tables.h -- extern everywhere except consts.cpp
// itself). These back the CREATE()/CREATE1()/RELEASE()/RECREATE() macros
// (utils.h) used PERVASIVELY throughout db_players.cpp/entity_lifecycle.cpp
// -- every allocation and every character/object teardown goes through
// them. Verbatim copy of utility.cpp's bodies (pure calloc/free wrappers
// with an allocation-failure abort, no comm/game dependency whatsoever) --
// not a substitute, the real thing.
// ===========================================================================
int global_release_flag = 1;

void* create_function(int elem_size, int elem_num, int line, std::string_view file)
{
    void* create_pointer;
    if (elem_size * elem_num == 0)
        create_pointer = calloc(1, 1);
    else
        create_pointer = calloc(elem_size, elem_num);

    if (!create_pointer) {
        const std::string file_owner(rots::text::truncate_at_null(file));
        printf("CREATE: could not allocate memory %d size %d elements at line %d, file %s.\n",
            elem_size, elem_num, line, file_owner.c_str());
        exit(0);
    }
    return create_pointer;
}

void free_function(void* pnt)
{
    if (pnt)
        free(pnt);
}

// ===========================================================================
// str_dup()/decrypt_line()/encrypt_line() -- utility.cpp. str_dup() backs
// file_to_string_alloc() below; decrypt_line()/encrypt_line() are the
// legacy password obfuscation cipher load_player_from_text()/
// write_player_text() (db_players.cpp) call on every text-pfile load/save --
// genuinely reachable for every non-account-JSON character this executable
// converts. All three are pure byte/buffer manipulation with zero comm/game
// dependency; verbatim copies of utility.cpp's current bodies.
// ===========================================================================
char* str_dup(const char* source)
{
    if (!source)
        return NULL;

    char* new_string;
    int length = std::strlen(source);

    CREATE(new_string, char, ((int)(length / 0x100) + 1) * 0x100);

    for (int i = 0; i < length; i++) {
        new_string[i] = source[i];
    }
    new_string[length] = 0;

    return new_string;
}

namespace {
unsigned char convert_stub_encrypt_line_lp[1000];
unsigned char convert_stub_decrypt_line_line[1000];
} // namespace

void encrypt_line(unsigned char* line, int len)
{
    unsigned char k1, k2;
    int tmp;
    unsigned char* lp = convert_stub_encrypt_line_lp;

    for (tmp = 0; tmp < len; tmp++)
        if (line[tmp] > 127)
            line[tmp] -= 128;

    for (tmp = 0; tmp < len - 1; tmp++) {
        k1 = (line[tmp] * 16);
        k2 = (line[tmp + 1] / 8);
        lp[tmp] = (k1 + k2) & 127;
        lp[tmp] += 32;
    }
    k1 = (line[len - 1] * 16);
    k2 = (line[0] / 8);
    lp[len - 1] = (k1 + k2) & 127;
    lp[len - 1] += 32;

    for (tmp = 0; tmp < len; tmp++)
        line[tmp] = lp[tmp];
}

void decrypt_line(unsigned char* lp, int len)
{
    unsigned char k1, k2;
    int tmp;
    unsigned char* line = convert_stub_decrypt_line_line;

    k1 = ((lp[len - 1] - 32) * 8);
    k2 = ((lp[0] - 32) / 16);
    line[0] = (k1 + k2) & 127;
    for (tmp = 1; tmp < len; tmp++) {
        k1 = ((lp[tmp - 1] - 32) * 8);
        k2 = ((lp[tmp] - 32) / 16);
        line[tmp] = (k1 + k2) & 127;
    }

    for (tmp = 0; tmp < len; tmp++)
        lp[tmp] = line[tmp];
}

// ===========================================================================
// file_to_string_alloc()/file_to_string() -- db_boot.cpp. load_player()
// (db_players.cpp) calls file_to_string_alloc() for every character whose
// player_table entry is NOT a ".character.json" (account-native) path --
// i.e. every legacy text-format pfile, genuinely reachable. Verbatim copy
// of db_boot.cpp's current bodies (plain fopen/fgets/fclose, no comm/game
// dependency).
// ===========================================================================
int file_to_string(std::string_view name, char* buf_out)
{
    const std::string name_owner(rots::text::truncate_at_null(name));
    FILE* fl;
    char tmp[100];

    *buf_out = '\0';

    if (!(fl = fopen(name_owner.c_str(), "r"))) {
        perror(std::format("Error reading {}", name_owner).c_str());
        *buf_out = '\0';
        return (-1);
    }

    do {
        fgets(tmp, 99, fl);

        if (!feof(fl)) {
            if (strlen(buf_out) + strlen(tmp) + 2 > MAX_STRING_LENGTH) {
                rots::log::write_stderr(
                    "SYSERR: fl->strng: string too big (convert_stubs.cc, file_to_string)");
                *buf_out = '\0';
                fclose(fl);
                return (-1);
            }

            strcat(buf_out, tmp);
            *(buf_out + strlen(buf_out) + 1) = '\0';
            *(buf_out + strlen(buf_out)) = '\r';
        }
    } while (!feof(fl));

    fclose(fl);

    return (0);
}

int file_to_string_alloc(std::string_view name, char** buf_ptr)
{
    char temp[MAX_STRING_LENGTH];

    if (file_to_string(name, temp) < 0)
        return -1;

    RELEASE(*buf_ptr);

    *buf_ptr = str_dup(temp);
    return 0;
}

// ===========================================================================
// str_cmp()/str_cmp_nullable() -- utility.cpp. save_char()/load_player()/
// load_player_from_text()/delete_character_file() (db_players.cpp) call
// these directly on every character processed -- genuinely reachable. Pure
// string comparison (case-insensitive via the LOWER macro, utils.h);
// verbatim copies of utility.cpp's current bodies.
// ===========================================================================
int str_cmp(std::string_view first, std::string_view second)
{
    for (std::size_t index = 0;; ++index) {
        const char first_char = (index < first.size()) ? first[index] : '\0';
        const char second_char = (index < second.size()) ? second[index] : '\0';
        if (first_char == '\0' || second_char == '\0') {
            if (first_char == second_char) {
                return 0;
            }
            return (first_char == '\0') ? -1 : 1;
        }
        const int difference = LOWER(first_char) - LOWER(second_char);
        if (difference < 0) {
            return -1;
        }
        if (difference > 0) {
            return 1;
        }
    }
}

int str_cmp_nullable(const char* first, const char* second)
{
    if (first == nullptr || second == nullptr) {
        if (first == second) {
            return 0;
        }
        return first == nullptr ? -1 : 1;
    }
    for (;; ++first, ++second) {
        const int difference = LOWER(*first) - LOWER(*second);
        if (difference != 0) {
            return (difference < 0) ? -1 : 1;
        }
        if (*first == '\0') {
            return 0;
        }
    }
}

// ===========================================================================
// rots_remove()/rots_rename_replace() -- utility.cpp. Genuinely
// platform-shaped (their own doc comments say "POSIX-*-semantics ... on
// every platform", the same job as the rest of rots_platform's
// rots_net.cpp/rots_crypt.cpp), but physically homed in utility.cpp today.
// The account-native codec (account_management.cpp) and the crime/exploit
// JSON codecs' atomic-write helpers (db_players.cpp) call these on every
// write/delete -- genuinely reachable for account-linked characters and
// every atomic-write path. Verbatim copies of utility.cpp's current
// POSIX-branch bodies (the #if defined PREDEF_PLATFORM_WINDOWS branches are
// omitted -- rots_convert is not yet built for Windows by any CI job this
// wave; see the report's gates section -- and would need the same
// MoveFileExA/RemoveDirectoryA Win32 calls utility.cpp's real body already
// makes, not a behavioral divergence, just an unexercised platform branch).
// Follow-on: relocate both into rots_platform properly (they have zero game
// dependency), at which point every consumer -- ageland and rots_convert
// alike -- links the one real, both-platform-complete definition.
// ===========================================================================
int rots_remove(std::string_view path)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
    return std::remove(path_owner.c_str());
}

int rots_rename_replace(std::string_view source_path, std::string_view destination_path)
{
    const std::string source_path_owner(rots::text::truncate_at_null(source_path));
    const std::string destination_path_owner(rots::text::truncate_at_null(destination_path));
    return std::rename(source_path_owner.c_str(), destination_path_owner.c_str());
}

// ===========================================================================
// get_race_weight()/get_race_height() -- utility.cpp. get_race_weight() is
// genuinely reachable: store_to_char() (db_players.cpp) calls it whenever a
// loaded character's weight is <= 200 ("weight fix!! should be removed some
// time"). get_race_height() is only reachable via init_char() (never called
// by this executable -- see convert_main.cpp), but is duplicated alongside
// its sibling for the same cost as a stub. Both pure switch-on-race; no
// further dependency. Verbatim copies of utility.cpp's current bodies.
// ===========================================================================
int get_race_weight(struct char_data* ch)
{
    int gender_mod = (GET_SEX(ch) == SEX_FEMALE) ? 8 : 10;

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 100000 * gender_mod / 10;
    case RACE_HUMAN:
        return 17000 * gender_mod / 10;
    case RACE_DWARF:
        return 20000 * gender_mod / 10;
    case RACE_WOOD:
        return 12000 * gender_mod / 10;
    case RACE_HOBBIT:
        return 7000 * gender_mod / 10;
    case RACE_HIGH:
        return 13000 * gender_mod / 10;
    case RACE_URUK:
        return 16000 * gender_mod / 10;
    case RACE_HARAD:
        return 17000 * gender_mod / 10;
    case RACE_ORC:
        return 9000 * gender_mod / 10;
    case RACE_EASTERLING:
        return 17000 * gender_mod / 10;
    case RACE_MAGUS:
        return 16000 * gender_mod / 10;
    case RACE_TROLL:
        return 80000 * gender_mod / 10;
    case RACE_BEORNING:
        return 80000 * gender_mod / 10;
    case RACE_OLOGHAI:
        return 40000 * gender_mod / 10;
    case RACE_HARADRIM:
        return 17000 * gender_mod / 10;
    case RACE_UNDEAD:
        return 5000 * gender_mod / 10;
    default:
        return 15000;
    }
}

int get_race_height(struct char_data* ch)
{
    int gender_mod = (GET_SEX(ch) == SEX_FEMALE) ? 9 : 10;

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 200 * gender_mod / 10;
    case RACE_HUMAN:
        return 180 * gender_mod / 10;
    case RACE_DWARF:
        return 130 * gender_mod / 10;
    case RACE_WOOD:
        return 200 * gender_mod / 10;
    case RACE_HOBBIT:
        return 110 * gender_mod / 10;
    case RACE_HIGH:
        return 210 * gender_mod / 10;
    case RACE_URUK:
        return 170 * gender_mod / 10;
    case RACE_HARAD:
        return 180 * gender_mod / 10;
    case RACE_ORC:
        return 120 * gender_mod / 10;
    case RACE_EASTERLING:
        return 180 * gender_mod / 10;
    case RACE_MAGUS:
        return 170 * gender_mod / 10;
    case RACE_TROLL:
        return 225 * gender_mod / 10;
    case RACE_UNDEAD:
        return 180 * gender_mod / 10;
    case RACE_HARADRIM:
        return 180 * gender_mod / 10;
    case RACE_BEORNING:
        return 225 * gender_mod / 10;
    case RACE_OLOGHAI:
        return 200 * gender_mod / 10;
    default:
        return 200;
    }
}

// ===========================================================================
// char_control_array / char_exists() / set_char_exists() /
// remove_char_exists() -- handler.cpp. remove_char_exists() is genuinely
// reachable: free_char() (entity_lifecycle.cpp) calls it on every character
// teardown (i.e. every convert_one_character() call, when the char_data_ptr
// goes out of scope). char_exists()/set_char_exists() are unreachable from
// this executable's call graph (only utils::is_riding()/is_ridden(),
// char_utils.cpp, call char_exists(); register_npc_char(), never linked
// here, calls set_char_exists()) but share the same trivial global bit
// array, so all three are duplicated together. This executable never calls
// register_npc_char() or any other world-registration function, so every
// character's ch->abs_number stays at clear_char()'s zero-initialized
// default -- remove_char_exists(0) on free_char() is therefore always an
// in-bounds, harmless bit-clear. Verbatim copy of handler.cpp's current
// bodies.
// ===========================================================================
namespace {
char convert_stub_char_control_array[MAX_CHARACTERS / 8 + 1];
} // namespace

int char_exists(int num) { return (convert_stub_char_control_array[num / 8] & (1 << (num % 8))); }

void set_char_exists(int num) { convert_stub_char_control_array[num / 8] |= (1 << (num % 8)); }

void remove_char_exists(int num) { convert_stub_char_control_array[num / 8] &= ~(1 << (num % 8)); }

// ===========================================================================
// number() -- utility.cpp. Only reachable via init_char() (never called by
// this executable -- see convert_main.cpp), so an exact copy (which also
// carries a TESTING-only rots_test_random_hook() indirection meaningless
// here) is not warranted. This is a simplified, but still REAL (not faked),
// implementation using rots_rng (rots_platform, already linked; per
// AGENTS.md "All game randomness flows through rots_rng" -- this executable
// follows the same rule rather than reintroducing rand()/random()).
// ===========================================================================
int number(int from, int to)
{
    if (from == to)
        return from;
    if (from > to)
        std::swap(to, from);

    const unsigned int span = static_cast<unsigned int>(to - from + 1);
    return from + static_cast<int>(rots_rng::next() % span);
}

// ===========================================================================
// clear_account_backed_object_bytes_for_character() -- objsave.cpp.
// Genuinely reachable: free_char() (entity_lifecycle.cpp) calls it on every
// character teardown. Its real body erases an entry from an in-memory-only
// staging map (g_staged_account_backed_object_data) keyed by the character,
// populated ONLY by stage_account_backed_object_data_for_character()
// (objsave.cpp), which is called ONLY from interpre.cpp's login flow --
// never on this executable's call graph (see convert_main.cpp). So for
// every character this executable ever constructs, that map entry was never
// populated in the first place: this is not an approximation of the real
// behavior, it is PROVABLY the same outcome (erasing a key that was never
// inserted is a no-op either way) -- true unlike this file's other
// "unreachable" stubs, which merely never fire rather than being proven
// equivalent when they would.
// ===========================================================================
void clear_account_backed_object_bytes_for_character(const struct char_data* ch) { (void)ch; }

// ===========================================================================
// Unreachable stubs -- each of these is DEFINED in a linked TU
// (db_players.cpp: rename_char()/read_crime_file()/add_crime()/
// know_of_crime()/forget_crimes(); entity_lifecycle.cpp: init_char()) that
// this executable's own call graph never invokes (convert_main.cpp calls
// only build_player_index()/load_char()/store_to_char()/save_char() -- see
// that file), but whose references still need to resolve for the whole TU
// to link. Each logs loudly if ever actually reached, since "never called"
// is exactly the kind of claim worth a tripwire rather than silent
// (potentially wrong) success.
// ===========================================================================
char* fname(char* namelist)
{
    rots::log::write_stderr(std::format(
        "rots_convert: STUB fname('{}') called -- unreachable (only "
        "utils::get_object_name(), never called by this executable's load/store/save flow).",
        namelist ? namelist : "(null)"));
    return namelist;
}

int other_side(const char_data* character, const char_data* other)
{
    (void)character;
    (void)other;
    rots::log::write_stderr(
        "rots_convert: STUB other_side() called -- unreachable (only "
        "utils::is_hostile_to(), never called by this executable's load/store/save flow).");
    return 0;
}

int isname_nullable(const char* query, const char* name_list, char full)
{
    (void)query;
    (void)name_list;
    (void)full;
    rots::log::write_stderr(
        "rots_convert: STUB isname_nullable() called -- unreachable (only "
        "obj_data::is_quiver(), never called by this executable's load/store/save flow).");
    return 0;
}

char unaccent(char c)
{
    rots::log::write_stderr(
        "rots_convert: STUB unaccent() called -- unreachable (only rename_char(), never "
        "called by this executable's load/store/save flow).");
    return c;
}

int find_name(char* name)
{
    rots::log::write_stderr(
        std::format("rots_convert: STUB find_name('{}') called -- unreachable (only rename_char(), "
                    "never called by this executable's load/store/save flow).",
            name ? name : "(null)"));
    return -1;
}

void set_title(struct char_data* ch)
{
    (void)ch;
    rots::log::write_stderr(
        "rots_convert: STUB set_title() called -- unreachable (only init_char(), never "
        "called by this executable's load/store/save flow).");
}

int Crash_get_filename(std::string_view original_name, char* filename)
{
    (void)original_name;
    if (filename)
        *filename = '\0';
    rots::log::write_stderr(
        "rots_convert: STUB Crash_get_filename() called -- unreachable (only rename_char(), "
        "never called by this executable's load/store/save flow).");
    return 0;
}

void add_exploit_record(int type, struct char_data* victim, int int_param, const char* extra)
{
    (void)type;
    (void)victim;
    (void)int_param;
    (void)extra;
    rots::log::write_stderr(
        "rots_convert: STUB add_exploit_record() called -- unreachable (only rename_char(), "
        "never called by this executable's load/store/save flow; the real home is db_boot.cpp's "
        "capture-not-codec add_exploit_record(), per the db-split plan's P/B classification).");
}

int find_player_in_table(std::string_view name, int idnum)
{
    (void)name;
    (void)idnum;
    rots::log::write_stderr(
        "rots_convert: STUB find_player_in_table() called -- unreachable (only "
        "read_crime_file()/add_crime()/know_of_crime()/forget_crimes()/rename_char(), never "
        "called by this executable's load/store/save flow).");
    return -1;
}

objects_json::ObjectSaveData build_default_account_backed_object_data()
{
    rots::log::write_stderr(
        "rots_convert: STUB build_default_account_backed_object_data() called -- unreachable "
        "(only load_object_save_data_for_character(), which this executable never calls -- see "
        "convert_main.cpp).");
    return objects_json::ObjectSaveData { };
}

sh_int* get_encumb_table()
{
    rots::log::write_stderr(
        "rots_convert: STUB get_encumb_table() called -- unreachable (only "
        "utils::get_encumbrance_weight()/get_encumbrance(), never called by this executable's "
        "load/store/save flow).");
    static sh_int placeholder[MAX_WEAR] = { };
    return placeholder;
}

sh_int* get_leg_encumb_table()
{
    rots::log::write_stderr(
        "rots_convert: STUB get_leg_encumb_table() called -- unreachable (only "
        "utils::get_leg_encumbrance(), never called by this executable's load/store/save flow).");
    static sh_int placeholder[MAX_WEAR] = { };
    return placeholder;
}

namespace utils {
bool is_room_outside(const room_data& room)
{
    (void)room;
    rots::log::write_stderr(
        "rots_convert: STUB utils::is_room_outside() called -- unreachable (only "
        "utils::can_see(), never called by this executable's load/store/save flow).");
    return false;
}

bool is_light(const room_data& room, const weather_data& weather)
{
    (void)room;
    (void)weather;
    rots::log::write_stderr(
        "rots_convert: STUB utils::is_light() called -- unreachable (only utils::can_see(), "
        "never called by this executable's load/store/save flow).");
    return true;
}
} // namespace utils

// ===========================================================================
// get_skill_array() -- consts.cpp. NOT a safe no-op: character_json.cpp's
// talk_key_for_index()/skill_key_for_index() call it to compose the
// human-readable JSON keys ("skill_fencing" style) the account-native
// character codec uses for BOTH writing (save_char()'s account-linked
// branch) and READING (load_player() -> load_player_from_account_json_path()
// for every ".character.json" player_table entry) -- so an empty/wrong
// .name here does not just produce cosmetically different keys, it makes
// load_player_from_account_json_path() FAIL to parse any skill/talk key it
// cannot map back to an index (skill_index_for_key() returns -1), which
// build_player_index()'s account-native index scan (db_players.cpp) treats
// as a FATAL error (exit(1)) -- confirmed empirically: an early placeholder
// version of this stub with empty .name fields crashed a functional smoke
// test against this repo's own lib/ data on the very first
// account-native character (deserialize_account_character_from_json:
// "Unknown skill key 'slashing'"). So this is a VERBATIM DATA duplicate of
// consts.cpp's skills[MAX_SKILLS] table's `.name` field only (positionally
// extracted from consts.cpp:382-634 at this writing -- every other field
// (type/level/spell_pointer/beats/targets/learn_diff/learn_type/is_fast/
// skill_spec) defaults to zero/null, which is safe because nothing this
// executable calls reads them: this file's simplified recalc_skills()
// above deliberately skips the only in-tree caller that would). The
// .spell_pointer entries themselves (consts.cpp's actual reason it can't be
// linked wholesale -- see this file's header comment and the affect-engine
// section's APPLY_SPELL case) are NOT reproduced; only the name strings,
// which are pure data with zero mystic.cpp/spell_pa.cpp coupling.
// Follow-on: once consts.cpp's function-pointer coupling is cut (see
// CMakeLists.txt's ROTS_CORE_SOURCES comment), the real skills[] table can
// be shared directly instead of this name-only duplicate, which will then
// need to be kept in sync by hand until that lands.
// ===========================================================================
const skill_data* get_skill_array()
{
    // Verbatim (name-only) data copy of consts.cpp:382-634's skills[] table,
    // positional (no [N] designators are used in the real table either, so
    // index N here means the same skill as index N there). A plain
    // string-literal array (not a skill_data aggregate initializer) so this
    // stays -Wmissing-field-initializers-clean without spelling out all 11
    // remaining zero/null fields per entry; kSkillNames[i] is copied into
    // table[i].name below, once, on first call.
    static const char* const kSkillNames[] = {
        "barehanded",
        "slashing",
        "concussion",
        "whips/flails",
        "piercing",
        "spears",
        "axes",
        "natural attacks",
        "swimming",
        "two-handed",
        "weapon mastery",
        "parry",
        "kick",
        "bash",
        "rescue",
        "berserk",
        "find weakness",
        "block exit",
        "wild swing",
        "leadership",
        "riposte",
        "dodge",
        "fast attack",
        "sneak",
        "hide",
        "ambush",
        "track",
        "pick lock",
        "search",
        "animals",
        "gather herbs",
        "stealth",
        "awareness",
        "ride",
        "accuracy",
        "tame",
        "calm",
        "whistle",
        "stalking",
        "travelling",
        "recruit",
        "detect hidden",
        "evasion",
        "poison",
        "resist poison",
        "curing saturation",
        "restlessness",
        "resist magic",
        "slow digestion",
        "dispel regeneration",
        "insight",
        "pragmatism",
        "haze",
        "fear",
        "divination",
        "rend",
        "sanctuary",
        "vitality",
        "terror",
        "refresh all",
        "enchant weapon",
        "archery",
        "summon",
        "hallucinate",
        "regeneration",
        "guardian",
        "infravision",
        "curse",
        "revive",
        "detect magic",
        "shift",
        "magic missile",
        "reveal life",
        "locate living",
        "cure self",
        "chill ray",
        "blink",
        "freeze",
        "lightning bolt",
        "vitalize self",
        "flash",
        "earthquake",
        "create light",
        "death ward",
        "dark bolt",
        "mist of baazunga",
        "mind block",
        "remove poison",
        "beacon",
        "protection",
        "blaze",
        "firebolt",
        "relocate",
        "cone of cold",
        "identify",
        "bend time",
        "fireball",
        "locate life",
        "searing darkness",
        "lightning strike",
        "word of pain",
        "word of sight",
        "word of agony",
        "shout of pain",
        "word of shock",
        "spear of darkness",
        "leach",
        "black arrow",
        "shield",
        "detect evil",
        "blind",
        "confuse",
        "expose elements",
        "bite",
        "swipe",
        "maul",
        "asphyxiation",
        "Power of Arda",
        "activity",
        "rage",
        "anger",
        "animal language",
        "human language",
        "orcish language",
        "mark",
        "trash",
        "trash",
        "nothing",
        "wind blast",
        "Fame War",
        "",
        "defend",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "smash",
        "frenzy",
        "stomp",
        "",
        "cleave",
        "overrun",
        "mass regeneration",
        "mass vitality",
        "mass insight",
        "",
        // Remaining MAX_SKILLS - 162 entries deliberately absent from this
        // literal -- the copy loop below leaves table[162..MAX_SKILLS) at
        // its static value-initialized (empty name) default, matching
        // consts.cpp's own array (declared MAX_SKILLS=256, only the first
        // 162 populated -- the rest default to a zero skill_data too).
    };
    constexpr int kNumSkillNames = sizeof(kSkillNames) / sizeof(kSkillNames[0]);
    static_assert(kNumSkillNames <= MAX_SKILLS, "kSkillNames must fit within MAX_SKILLS");

    static skill_data table[MAX_SKILLS] = { };
    static bool initialized = false;
    if (!initialized) {
        for (int i = 0; i < kNumSkillNames; ++i) {
            std::snprintf(table[i].name, sizeof(table[i].name), "%s", kSkillNames[i]);
        }
        initialized = true;
    }
    return table;
}

// ===========================================================================
// language_number / language_skills[] -- consts.cpp. Same reachability class
// as get_skill_array() immediately above (talk_key_for_index(),
// character_json.cpp) and the same "not a safe placeholder" lesson learned
// from it (see that entry's account-native-index exit(1) crash report) --
// verbatim data copy of consts.cpp:636-638's current values rather than an
// empty/zero placeholder.
// ===========================================================================
byte language_number = 3;
byte language_skills[] = { LANG_ANIMAL, LANG_HUMAN, LANG_ORC };

// ===========================================================================
// race_abbrevs[] -- consts.cpp. Referenced only by account_management.cpp's
// safe_race_abbrev(), which is called only by format_account_character_short_entry()/
// format_account_character_short_roster() -- login-menu character-roster
// display formatting, never called by this executable's load/store/save
// flow (see convert_main.cpp). A default-constructed std::string_view has
// `.data() == nullptr`, which safe_race_abbrev() ALREADY guards
// (`if (::race_abbrevs[race].data() == nullptr) return "??";`) -- so a
// zero-initialized (all-empty) array is not an approximation, it exercises
// safe_race_abbrev()'s own existing fallback path exactly as the real table
// would for any race index it didn't recognize.
// ===========================================================================
extern const std::string_view race_abbrevs[MAX_RACES + 40] = { };

// ===========================================================================
// square_root[] -- consts.cpp. Referenced by char_utils.cpp's
// utils::get_prof_coof() (via the GET_PROF_COOF macro, utils.h), which
// nothing in this executable's load/store/save call graph invokes (this
// file's simplified recalc_skills() above deliberately does not call it --
// see that entry). Zero-filled placeholder, matching consts.cpp's real
// declared size (consts.cpp:2138, `sh_int square_root[171]`).
// ===========================================================================
sh_int square_root[171] = { };

// ===========================================================================
// Crash_delete_file() -- objsave.cpp (deliberately OUT this wave -- see the
// task brief: "objsave/boards/mail/pkill are deliberately OUT this wave;
// their welds are catalogued follow-on work"). build_player_index()
// (db_players.cpp) calls it from an auto-delete-inactive-low-level-
// characters branch that is gated behind a LOCAL `const bool
// enable_auto_delete = false;` -- permanently disabled at boot, by design
// (see db_players.cpp's comment on that flag: imported/legacy characters
// must never be silently auto-removed). Every preset here configures
// CMAKE_BUILD_TYPE=Debug (-O0, see CMakePresets.json's `base` preset), so
// the compiler does not fold `enable_auto_delete && ...` down to a
// constant-false branch and eliminate this call -- the reference survives
// to link time even though it is UNREACHABLE at runtime for every build this
// repo produces. This stub is therefore never called; it exists purely to
// satisfy the linker; the loud log line is a tripwire in case that analysis
// is ever wrong (a future edit flips enable_auto_delete to true).
// Follow-on: hoist the disabled branch behind a real seam (a
// std::function callback db_boot.cpp/comm.cpp registers, mirroring the
// logging-seam pattern) so persist code never names objsave.cpp's
// Crash_delete_file at all, clean or dead.
// ===========================================================================
int Crash_delete_file(std::string_view name)
{
    rots::log::write_stderr(
        std::format("rots_convert: STUB Crash_delete_file('{}') called -- this should be "
                    "unreachable (build_player_index()'s auto-delete branch is permanently "
                    "disabled). Returning 0 without touching any file.",
            name));
    return 0;
}

// ===========================================================================
// send_to_char() (both overloads) / vsend_to_char() / act() -- comm.cpp.
// Every call site reachable from db_players.cpp/entity_lifecycle.cpp/
// char_utils.cpp/convert_exploits.cpp/convert_plrobjs.cpp is either:
//   - save_char()'s notify_char-gated "Saving X." message -- convert_main.cpp
//     always passes notify_char=0, so this branch is never taken;
//   - a "you are not in the character list" / "you are not being saved"
//     defensive branch that only fires when a JUST-LOOKED-UP player_table
//     name search fails to re-find the very name it was given -- can't
//     happen for a character this executable is iterating straight out of
//     player_table itself;
//   - inside an ACMD(do_...) command handler (act_othe.cpp-style callers,
//     or convert_exploits.cpp's/convert_plrobjs.cpp's own
//     do_convert_exploits/do_convert_plrobjs ACMDs) that only a live player
//     typing a command reaches -- convert_main.cpp never calls an ACMD.
// So every one of these is DEFINED (whole-TU linking) but never CALLED by
// this executable. Logged if ever hit, since "never called" is exactly the
// kind of claim that is worth a loud tripwire instead of silent success.
// Follow-on: none needed for correctness (already unreachable); shrinks
// automatically once the app-layer TUs that only exist as command-handler
// wrappers around persist codecs (convert_exploits.cpp/convert_plrobjs.cpp's
// ACMDs) are split out of the persist-codec halves those files also carry.
// ===========================================================================
void send_to_char(std::string_view message, struct char_data* character)
{
    (void)character;
    rots::log::write_stderr(std::format(
        "rots_convert: STUB send_to_char(message, char_data*) called (message: '{}') -- "
        "this should be unreachable from the converter's load/store/save flow.",
        message));
}

void send_to_char(std::string_view message, int character_id)
{
    rots::log::write_stderr(
        std::format("rots_convert: STUB send_to_char(message, id={}) called (message: '{}') -- "
                    "this should be unreachable from the converter's load/store/save flow.",
            character_id, message));
}

void vsend_to_char(struct char_data* ch, const char* format, ...)
{
    (void)ch;
    rots::log::write_stderr(
        std::format("rots_convert: STUB vsend_to_char() called (format: '{}') -- this should be "
                    "unreachable from the converter's load/store/save flow.",
            format ? format : "(null)"));
}

void act(std::string_view str, int hide_invisible, struct char_data* ch, struct obj_data* obj,
    void* vict_obj, int type, char spam_only)
{
    (void)hide_invisible;
    (void)ch;
    (void)obj;
    (void)vict_obj;
    (void)type;
    (void)spam_only;
    rots::log::write_stderr(
        std::format("rots_convert: STUB act('{}') called -- this should be unreachable from the "
                    "converter's load/store/save flow.",
            str));
}

// ===========================================================================
// track_specialized_mage() / untrack_specialized_mage() -- comm.cpp. UNLIKE
// this file's other unreachable stubs, these ARE genuinely on the
// converter's call graph: store_to_char() (db_players.cpp) calls
// utils::set_specialization() (char_utils.cpp) twice per character loaded,
// and set_specialization() calls untrack_specialized_mage()/
// track_specialized_mage() whenever the OLD/NEW specialization is a mage
// spec (extra_specialization_data.is_mage_spec()). The real implementations
// maintain comm.cpp's file-local `specialized_mages` vector, an
// in-memory-only live-broadcast bookkeeping list (used to target
// mage-specialization-wide messages at connected players) -- it holds no
// persisted state and is never read back by anything save_char()/
// char_to_store() write to disk, so a safe no-op here cannot diverge
// rots_convert's on-disk output from a live login's. Logged (not silent)
// because "genuinely reachable, safe no-op" is exactly the class of stub
// most likely to bite if that invariant ever changes.
// Follow-on: same as the comm.cpp weld class generally -- once
// specialized_mages tracking moves behind a real interface (e.g. a
// mage-roster system rots_combat/rots_app registers with rots_entity,
// mirroring the logging seam's Sink registration pattern), this executable
// can link the real, empty-registry-by-default implementation instead of a
// duplicate no-op.
// ===========================================================================
void track_specialized_mage(char_data* mage)
{
    rots::log::write_stderr(
        std::format("rots_convert: STUB track_specialized_mage({}) -- no-op (converter has no live "
                    "specialized-mage broadcast roster to maintain).",
            static_cast<const void*>(mage)));
}

void untrack_specialized_mage(char_data* mage)
{
    rots::log::write_stderr(std::format(
        "rots_convert: STUB untrack_specialized_mage({}) -- no-op (converter has no live "
        "specialized-mage broadcast roster to maintain).",
        static_cast<const void*>(mage)));
}

// ===========================================================================
// player_spec::wild_fighting_handler (ctor) / get_attack_speed_multiplier()
// -- wild_fighting_handler.cpp (combat, app layer). The only caller inside
// this executable's linked TUs is char_utils.cpp's get_energy_regen(),
// which is not on the load_char()/store_to_char()/save_char() call graph
// (nothing in db_players.cpp/entity_lifecycle.cpp/char_utils.cpp itself
// calls get_energy_regen() -- it is a live-combat energy-regen-rate query).
// Follow-on: dissolves once get_energy_regen() (and the rest of
// char_utils.cpp's combat-facing helpers) move into a rots_combat-tier TU
// separate from the identity/spec accessors rots_convert genuinely needs.
// ===========================================================================
player_spec::wild_fighting_handler::wild_fighting_handler(char_data* in_character)
    : character(in_character)
{
    rots::log::write_stderr(
        "rots_convert: STUB player_spec::wild_fighting_handler::wild_fighting_handler() "
        "constructed -- this should be unreachable from the converter's load/store/save flow.");
}

float player_spec::wild_fighting_handler::get_attack_speed_multiplier() const
{
    rots::log::write_stderr(
        "rots_convert: STUB player_spec::wild_fighting_handler::get_attack_speed_multiplier() "
        "called -- this should be unreachable from the converter's load/store/save flow.");
    return 1.0f;
}

// ===========================================================================
// get_hit_text() -- fight.cpp (combat, app layer). The only caller inside
// this executable's linked TUs is char_utils.cpp's
// player_damage_details::get_damage_report(), a `score`-style report
// formatter not on the load_char()/store_to_char()/save_char() call graph.
// Returns a reference to a static empty-string pair rather than indexing
// fight.cpp's real attack_hit_text[] table (not linked here).
// Follow-on: dissolves once get_damage_report() (and the rest of
// char_utils.cpp's presentation-facing helpers) move out of the TU
// rots_convert links, alongside get_energy_regen() above.
// ===========================================================================
const attack_hit_type& get_hit_text(int w_type)
{
    rots::log::write_stderr(std::format(
        "rots_convert: STUB get_hit_text({}) called -- this should be unreachable from the "
        "converter's load/store/save flow.",
        w_type));
    static const attack_hit_type unreachable_placeholder { "", "" };
    return unreachable_placeholder;
}

// ===========================================================================
// world_room_vnum() -- db_world.cpp (the Task 1 persist/world seam; declared
// db.h, defined db_world.cpp -- NOT linked here, since rots_convert excludes
// every W-classified TU by design). save_char()'s ONLY call site
// (db_players.cpp) is `if ((load_room == NOWHERE) && (ch->in_room !=
// NOWHERE)) load_room = world_room_vnum(ch->in_room);`.
//
// convert_main.cpp's load-room checkpoint analysis (see that file's top
// comment) proves this branch is UNREACHABLE for every character this
// executable converts: it always calls
// `save_char(character.get(), character->specials2.load_room, 0)`, mirroring
// interpre.cpp's own just-loaded/not-yet-in-world call sites, and
// store_to_char() already set character->in_room to that exact same
// character->specials2.load_room value -- so `load_room == ch->in_room`
// always holds at this guard, and `load_room == NOWHERE` /
// `ch->in_room != NOWHERE` can never both be true simultaneously.
//
// This stub therefore does NOT need to reproduce world_room_vnum()'s real
// "return world[room_index].number" behavior (rots_convert links no world[]
// data to compute that from) -- it returns NOWHERE and logs loudly, so that
// IF this proof is ever invalidated by a future change to convert_main.cpp's
// call convention, the resulting bogus load_room is impossible to miss
// (both the stderr line and the on-disk load_room going visibly wrong,
// rather than a quiet corrupted-but-plausible room vnum).
// Follow-on: none needed for correctness (already unreachable by proof, not
// by omission) -- this entry stays until rots_convert either grows a real
// need for room data (it shouldn't -- that would reintroduce the exact
// persist->world coupling Task 1's seam was cut to remove) or db_players.cpp
// is refactored to make the always-non-NOWHERE call convention structurally
// guaranteed (e.g. a save_char() overload that doesn't take a load_room at
// all for the "just loaded, not in world" case).
// ===========================================================================
int world_room_vnum(int room_index)
{
    rots::log::write_stderr(std::format(
        "rots_convert: STUB world_room_vnum({}) called -- this should be PROVABLY "
        "UNREACHABLE (see convert_main.cpp's load-room checkpoint comment). Returning "
        "NOWHERE; if you are seeing this, convert_main.cpp's save_char() call convention "
        "changed and the load-room proof needs re-checking.",
        room_index));
    return NOWHERE;
}
