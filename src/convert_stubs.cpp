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
//
// db-split Task 4b is a worked example of that intended shrinkage: the
// ~14-function affect/derived-ability engine (affect_modify/affect_total/
// affect_to_char/affect_remove/affect_naked/apply_gear_affects/
// modify_affects/affected_by_spell/recalc_abilities/get_race_perception/
// get_naked_perception/get_naked_willpower/get_confuse_modifier/
// encrypt_line/decrypt_line) that used to be hand-duplicated here was
// relocated verbatim into entity_lifecycle.cpp instead, so ageland and
// rots_convert now link the one real definition of each. What remains here
// for that engine is a handful of small, still-genuinely-necessary
// stand-ins for symbols whose origin TU (handler.cpp/profs.cpp/consts.cpp/
// wild_fighting_handler.cpp) still is not linked into this executable --
// see the sections below (get_from_affected_type_pool/
// put_to_affected_type_pool/class_HP/pool_to_list/from_list_to_pool/
// affected_list/max_race_str/skills[]/player_spec::weapon_master_handler/
// get_current_time_phase).

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
#include <cerrno>
#include <cmath>
#include <cstdio>
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
// modify_affects()/affect_naked()/affected_by_spell() (formerly handler.cpp)
// and recalc_abilities()/get_race_perception()/get_naked_perception()/
// get_naked_willpower()/get_confuse_modifier() (formerly profs.cpp/
// utility.cpp) USED TO be duplicated in this file (see this file's git
// history before db-split Task 4b). store_to_char()/char_to_store() call
// into this engine unconditionally for every character, so a hand-written
// duplicate here was never optional -- and any accidental divergence from
// the real bodies would silently corrupt persisted derived-stat fields for
// every converted character. Task 4b resolved that risk at the root: the
// real bodies now live in entity_lifecycle.cpp (relocated verbatim from
// their origin TUs), which this executable already links directly, so
// rots_convert and ageland now execute the exact SAME single definition of
// each of those symbols. This section is gone; see entity_lifecycle.cpp's
// "Affect / derived-ability engine" section instead.
//
// Two small pieces of that engine's SUPPORT machinery remain here, because
// each is a real (not simplified) stand-in for a symbol whose origin TU
// still is not linked into this executable:
//
//   - get_from_affected_type_pool()/put_to_affected_type_pool() -- real
//     name, handler.cpp's affected_type allocator. handler.cpp itself is
//     NOT relocated (affect_to_room()/affect_remove_room(), the room-affect
//     analogues of the now-relocated affect_to_char()/affect_remove(), also
//     call these two helpers and stay in handler.cpp -- see the db-split
//     Task 4b plan section's shared-helper rule), so entity_lifecycle.cpp's
//     relocated affect_to_char()/affect_remove() still need an external
//     definition when handler.cpp isn't linked. Simplified relative to
//     handler.cpp's real allocator (CREATE+free, no free-list reuse -- a
//     converter processes one character at a time and has no long server
//     lifetime to amortize the pool optimization over); produces
//     byte-identical affected_type field CONTENTS either way, only the
//     allocation strategy differs.
//   - pool_to_list()/from_list_to_pool() (real names, utility.cpp) plus the
//     affected_list/affected_list_pool globals (real names, handler.cpp) and
//     the universal_list_counter/used_in_universal_list diagnostic counter
//     globals the two functions' bodies increment/decrement (real names,
//     utility.cpp) -- affect_to_char()'s live-tick bookkeeping registration
//     (`if (!ch->affected) { tmplist = pool_to_list(&affected_list,
//     &affected_list_pool); ... }`) is part of its now-relocated, verbatim
//     body, so it genuinely executes on this executable's call path (the
//     first affect applied to any character with no prior affects). Unlike
//     the allocator above, these ARE byte-verbatim copies of utility.cpp's
//     current bodies (pure linked-list bookkeeping, zero comm/game
//     dependency, including the counter increments/decrements) -- not
//     simplified, because they are cheap to copy exactly and this
//     bookkeeping (list and counters alike), once written, is read back by
//     nothing this executable's output depends on (the list is drained by
//     free_char()'s affect_remove() loop before the char_data_ptr goes out
//     of scope, same as a live server's logout path; the counters are pure
//     diagnostics with no reader on this executable's call path at all).
//   - class_HP() (real name, profs.cpp) -- recalc_abilities()'s HP formula
//     calls it. profs.cpp is NOT relocated (class_HP() is also used by
//     _INTERNAL::stat_assigner::organize(), profs.cpp's character-creation
//     stat-ordering helper, which this executable never reaches but which
//     keeps class_HP() defined there per the same shared-helper rule).
//     Byte-verbatim copy of profs.cpp's current body.
//   - max_race_str[] (real name, consts.cpp) -- recalc_abilities()'s
//     GET_BAL_STR() macro (utils.h) reads it. consts.cpp as a whole TU is
//     not linked here (see this file's header comment and the
//     race_affect[]/get_skill_array() entries below for why); this
//     executable's own equipment-always-null invariant (see
//     convert_main.cpp) means recalc_abilities()'s weapon branch --
//     the only code that reads max_race_str[] -- never actually executes
//     here, but the reference must still resolve at link time (the flat
//     build links whole .cpp files; see this file's header comment).
//     Verbatim data copy of consts.cpp's current values.
//   - player_spec::weapon_master_handler (ctor + get_attack_speed_multiplier(),
//     wild_fighting_handler.cpp/warrior_spec_handlers.h) -- recalc_abilities()'s
//     weapon branch constructs one. Same unreachable-by-invariant status as
//     max_race_str[] immediately above (equipment is always null, so the
//     `if (weapon)` branch this class lives in never runs here) and the
//     same "must still resolve at link time" requirement; stubbed following
//     this file's existing player_spec::wild_fighting_handler entry (further
//     below) rather than duplicated, since neither method's real body has
//     any bearing on persisted output from an unreachable call site.
// ===========================================================================
// ===========================================================================
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
// snapshot would be. Genuinely reachable: entity_lifecycle.cpp's relocated
// affect_to_char() (db-split Task 4b) calls it for every affect applied.
// ===========================================================================
char get_current_time_phase() { return 0; }

struct affected_type* get_from_affected_type_pool()
{
    struct affected_type* afnew;
    CREATE(afnew, struct affected_type, 1);
    return afnew;
}

void put_to_affected_type_pool(struct affected_type* oldaf) { free(oldaf); }

// Verbatim copy of utility.cpp's pool_to_list()/from_list_to_pool() (pure
// universal_list bookkeeping) plus the affected_list/affected_list_pool
// globals they and entity_lifecycle.cpp's relocated affect_to_char()/
// affect_remove() operate on (real names, normally defined in handler.cpp,
// which this executable does not link). universal_list_counter/
// used_in_universal_list are utility.cpp's real diagnostic counter globals
// that the verbatim bodies below increment/decrement as a side effect;
// they are inert here (read by nothing this executable's output depends
// on) but are declared, with utility.cpp's exact names and types, so the
// copied bodies are byte-verbatim rather than eliding that bookkeeping.
universal_list* affected_list = 0;
universal_list* affected_list_pool = 0;
int universal_list_counter = 0;
int used_in_universal_list = 0;

struct universal_list*
pool_to_list(struct universal_list** list, struct universal_list** head)
{
    struct universal_list* tmplist;

    if (*head) {
        tmplist = *head;
        *head = tmplist->next;
        used_in_universal_list++;
    } else {
        CREATE1(tmplist, universal_list);
        universal_list_counter++;
        used_in_universal_list++;
    }

    tmplist->next = *list;
    *list = tmplist;

    return tmplist;
}

void from_list_to_pool(universal_list** list, universal_list**, universal_list* body)
{
    if (*list == body) {
        *list = body->next;
    } else {
        universal_list* tmplist = NULL;
        for (tmplist = *list; tmplist->next; tmplist = tmplist->next) {
            if (tmplist->next == body) {
                break;
            }
        }

        if (tmplist->next == body) {
            tmplist->next = body->next;
        }
    }

    /* Thus not putting universal lists into a pool, but freeing the memory */
    used_in_universal_list--;
    universal_list_counter++; /* added because we are freeing body */

    free(body);
}

// Verbatim copy of profs.cpp's inline class_HP() -- uses
// utils::get_prof_points (char_utils.cpp, already linked) and GET_RACE.
int class_HP(const char_data* character)
{
    double hp_coofs = 3 * utils::get_prof_points(PROF_WARRIOR, *character) + 2 * utils::get_prof_points(PROF_RANGER, *character) + utils::get_prof_points(PROF_CLERIC, *character);

    if (GET_RACE(character) == RACE_ORC) {
        hp_coofs = hp_coofs * 4.0 / 7.0;
    }

    return int(std::sqrt(hp_coofs) * 200.0);
}

// Verbatim data copy of consts.cpp's max_race_str[MAX_RACES] table.
int max_race_str[MAX_RACES] = {
    22, // God
    22, // Human
    22, // Dwarf
    22, // Wood Elf
    22, // Hobbit
    22, // High Elf
    22, // Beorning
    22, // !UNUSED!
    22, // !UNUSED!
    22, // !UNUSED!
    22, // !UNUSED!
    22, // Uruk-Hai
    22, // !NPC - Harad!
    22, // Common Orc
    22, // !NPC - Easterling!
    22, // Uruk-Lhuth
    22, // !NPC - Undead!
    22, // Olog-Hai
    22, // Haradrim
    22, // !UNUSED!
    22, // !NPC - Troll!
    22 // !UNUSED!
};

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
// str_dup() -- utility.cpp; backs file_to_string_alloc() below. Pure
// byte/buffer manipulation with zero comm/game dependency; verbatim copy
// of utility.cpp's current body.
//
// decrypt_line()/encrypt_line() (the legacy password obfuscation cipher)
// used to be duplicated here too; db-split Task 4b relocated the REAL
// bodies to entity_lifecycle.cpp (which this executable links directly),
// so this executable now calls the one real definition instead of a
// second copy. See entity_lifecycle.cpp.
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
                // No fclose(fl) here -- matching db_boot.cpp's file_to_string()
                // exactly (this error path does not close fl there either);
                // see this section's header comment.
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
// every atomic-write path. Verbatim copies of utility.cpp's current bodies,
// INCLUDING the #if defined PREDEF_PLATFORM_WINDOWS branches -- rots_convert
// IS built by every CI job (see CMakeLists.txt's rots_convert comment: "added
// to `all` ... so every CI job builds it"), windows-msvc among them, so the
// Win32 MoveFileExA/RemoveDirectoryA branches are load-bearing here too, not
// an unexercised platform.
// Follow-on: relocate both into rots_platform properly (they have zero game
// dependency), at which point every consumer -- ageland and rots_convert
// alike -- links the one real, both-platform-complete definition.
// ===========================================================================
int rots_remove(std::string_view path)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
#if defined PREDEF_PLATFORM_WINDOWS
    if (std::remove(path_owner.c_str()) == 0) {
        return 0;
    }

    // CRT remove() rejects a directory with EACCES; retry as a directory the
    // way POSIX remove() falls back to rmdir(). RemoveDirectoryA is the Win32
    // primitive (same header set MoveFileExA above comes from); map its common
    // failures onto errno so callers' strerror(errno) messages stay meaningful.
    const DWORD attributes = GetFileAttributesA(path_owner.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if (RemoveDirectoryA(path_owner.c_str())) {
            return 0;
        }
        switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            errno = ENOENT;
            break;
        case ERROR_DIR_NOT_EMPTY:
            errno = ENOTEMPTY;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            errno = EACCES;
            break;
        default:
            errno = EIO;
            break;
        }
    }
    return -1;
#else
    return std::remove(path_owner.c_str());
#endif
}

int rots_rename_replace(std::string_view source_path, std::string_view destination_path)
{
    const std::string source_path_owner(rots::text::truncate_at_null(source_path));
    const std::string destination_path_owner(rots::text::truncate_at_null(destination_path));
#if defined PREDEF_PLATFORM_WINDOWS
    // MoveFileExA + MOVEFILE_REPLACE_EXISTING is the Win32 primitive with
    // exactly POSIX rename()'s replace behavior (atomic on NTFS same-volume
    // moves, which every persistence-layer temp file is -- the temp lives
    // next to its final path by construction).
    if (MoveFileExA(source_path_owner.c_str(), destination_path_owner.c_str(),
            MOVEFILE_REPLACE_EXISTING)) {
        return 0;
    }

    // Map the common failure causes onto errno so the call sites' existing
    // strerror(errno)-based error messages describe the real problem instead
    // of whatever stale errno was lying around.
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        errno = ENOENT;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        errno = EACCES;
        break;
    default:
        errno = EIO;
        break;
    }
    return -1;
#else
    return std::rename(source_path_owner.c_str(), destination_path_owner.c_str());
#endif
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
// linked wholesale -- see this file's header comment) are NOT reproduced;
// only the name strings, which are pure data with zero mystic.cpp/
// spell_pa.cpp coupling -- so every entry's .spell_pointer is null, which
// is exactly the value entity_lifecycle.cpp's relocated affect_modify()
// (db-split Task 4b) needs: its `case APPLY_SPELL:` guards on `if
// (!skills[tmp].spell_pointer) break;` before dispatching through the
// pointer, so a null-pointer table makes that case a clean, self-documented
// no-op here instead of a dangling-pointer call.
//
// Task 4b also exposed the backing table as the real global `skills[]`
// (not just reachable through get_skill_array()'s return value), because
// affect_modify() reads `skills[tmp]` directly (matching handler.cpp's own
// `extern struct skill_data skills[];` reference) rather than going through
// the accessor function.
// Follow-on: once consts.cpp's function-pointer coupling is cut (see
// CMakeLists.txt's ROTS_CORE_SOURCES comment), the real skills[] table can
// be shared directly instead of this name-only duplicate, which will then
// need to be kept in sync by hand until that lands.
// ===========================================================================

// Verbatim (name-only) data copy of consts.cpp:382-634's skills[] table,
// positional (no [N] designators are used in the real table either, so
// index N here means the same skill as index N there); every other field
// (type/level/spell_pointer/beats/targets/learn_diff/learn_type/is_fast/
// skill_spec) defaults to zero/null via value-initialization. Populated
// lazily (see get_skill_array() below) so the .name copy loop runs once;
// affect_modify()'s direct `skills[tmp].spell_pointer` reads never need
// .name populated (that field is untouched by the null-pointer guard), so
// this array is safe to read before get_skill_array() is ever called too.
struct skill_data skills[MAX_SKILLS] = { };

const skill_data* get_skill_array()
{
    // A plain string-literal array (not a skill_data aggregate initializer)
    // so this stays -Wmissing-field-initializers-clean without spelling out
    // all 11 remaining zero/null fields per entry; kSkillNames[i] is copied
    // into skills[i].name below, once, on first call.
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

    static bool initialized = false;
    if (!initialized) {
        for (int i = 0; i < kNumSkillNames; ++i) {
            std::snprintf(skills[i].name, sizeof(skills[i].name), "%s", kSkillNames[i]);
        }
        initialized = true;
    }
    return skills;
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
// player_spec::weapon_master_handler (single-arg ctor) / get_attack_speed_multiplier()
// -- wild_fighting_handler.cpp (combat, app layer; a DIFFERENT class from
// player_spec::wild_fighting_handler immediately above, despite the similar
// name and the same real-implementation TU). The only caller inside this
// executable's linked TUs is entity_lifecycle.cpp's relocated
// recalc_abilities() (db-split Task 4b), inside its `if (weapon)` branch --
// unreachable-by-invariant here, same as max_race_str[] above (this
// executable's own ch->equipment[] is always null; see convert_main.cpp),
// but the reference must still resolve at link time (the flat build links
// whole .cpp files; see this file's header comment). Stubbed following this
// file's existing player_spec::wild_fighting_handler entry immediately
// above rather than duplicating wild_fighting_handler.cpp's real (and,
// unlike the class above, weapon-and-spec-dependent) logic.
// Follow-on: dissolves once recalc_abilities()'s weapon branch (and the
// rest of the combat-facing derived-stat math) moves into a rots_combat-tier
// TU separate from the identity/persisted-stat accessors rots_convert
// genuinely needs.
// ===========================================================================
player_spec::weapon_master_handler::weapon_master_handler(char_data* in_character)
    : character(in_character)
{
    rots::log::write_stderr(
        "rots_convert: STUB player_spec::weapon_master_handler::weapon_master_handler() "
        "constructed -- this should be unreachable from the converter's load/store/save flow.");
}

float player_spec::weapon_master_handler::get_attack_speed_multiplier() const
{
    rots::log::write_stderr(
        "rots_convert: STUB player_spec::weapon_master_handler::get_attack_speed_multiplier() "
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
