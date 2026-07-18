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
// rots_convert now link the one real definition of each. entity-seed Task 5
// (below) finished that engine's remaining small stand-ins the same way:
// get_from_affected_type_pool/put_to_affected_type_pool/class_HP/
// pool_to_list/from_list_to_pool/affected_list/affected_list_pool/
// get_current_time_phase (+ int pulse's definition) are now real
// entity_lifecycle.cpp definitions too, so those stub sections are gone;
// player_spec::weapon_master_handler's ctor/get_attack_speed_multiplier()
// stub is gone the same way, superseded by entity_hooks.h's
// attack-speed-multiplier hook (recalc_abilities() dispatches through it
// instead of constructing the handler directly).
//
// entity-seed Task 2 (skills[] weld cut, spec Sec3/Sec10 step 4) joined
// consts.cpp itself into rots_core, so rots_convert now links the REAL
// race_affect[]/max_race_str[]/skills[]/get_skill_array()/language_number/
// language_skills/race_abbrevs[]/square_root[]/global_release_flag/
// get_encumb_table()/get_leg_encumb_table() straight from consts.cpp -- the
// verbatim data duplicates this file used to carry for those symbols are
// gone (see this file's git history, pre-entity-seed-Task-2, for their
// prior stub text). create_function()/free_function() were still
// duplicated below at that point; their real home was utility.cpp, not
// consts.cpp, and that weld was unrelated follow-on work -- entity-seed
// Task 4 (below) resolved it.
//
// entity-seed Task 3 (send_to_char/act output seam, spec Sec13) deletes the
// six send_to_char()/vsend_to_char()/act()/track_specialized_mage()/
// untrack_specialized_mage() stubs the same way: output_seam.cpp now joins
// rots_core and defines all five global symbols as forwarders through a
// null-defaulted Sinks aggregate, so rots_convert (never calling
// register_game_output_sinks(), an app-layer/comm.cpp-only boot step) gets
// the same tripwire-logged no-op behavior these stubs used to hand-carry
// (see this file's git history, pre-entity-seed-Task-3, for the prior stub
// text and per-symbol reachability analysis).
//
// entity-seed Task 4 (platform-helper relocations) deletes ten more stubs
// the same way: log()/mudlog() (formerly utility.cpp, already pure
// rots::log::write*() forwarders) now join rots_log.cpp, and
// create_function()/free_function()/str_dup()/str_cmp()/str_cmp_nullable()/
// rots_remove()/rots_rename_replace()/number(int,int) (formerly utility.cpp,
// all platform-pure -- no comm/game dependency) now live in the new
// rots_util.cpp -- both TUs join rots_platform (spec Sec13's L0 layer), so
// rots_convert links the one real definition of each of these ten symbols
// through RotS::platform instead of a second hand-duplicated copy. The
// number(int,int) real definition (rots_util.cpp) restores the TESTING
// hook-consulting behavior this file's own simplified stand-in explicitly
// omitted (see that stub's prior comment, this file's git history
// pre-entity-seed-Task-4) -- a strict improvement, not a behavior change
// this executable's own call graph can observe (init_char(), the only
// caller, is never reached here either way; see convert_main.cpp).
// (see this file's git history, pre-entity-seed-Task-4, for the ten stubs'
// prior text and per-symbol reachability analysis.)
//
// entity-seed Task 6 (rots_entity static library stand-up) deletes the
// utils::is_room_outside()/utils::is_light() stubs the same way:
// environment_utils.cpp now joins entity_lifecycle.cpp/object_utils.cpp in
// the new RotS::entity library (linked below rots_convert), so those two
// real definitions arrive from the archive instead of a hand-duplicated
// stand-in. Neither was ever reached by this executable's own call graph
// (only utils::can_see() called them, which rots_convert never calls) --
// see this file's git history, pre-entity-seed-Task-6, for the two stubs'
// prior text.
//
// persist-split PS Task 3 (converter re-plumb onto obj_files.cpp) deletes
// three more stubs the same way: Crash_get_filename()/Crash_delete_file()/
// build_default_account_backed_object_data() are now real obj_files.cpp
// definitions (PS Task 2 carved that TU's P-half out of objsave.cpp; this
// task is what first links it into rots_convert), so rots_convert links
// the one real definition of each instead of a hand-duplicated stand-in.
// db_players.cpp -- the only caller of Crash_get_filename/Crash_delete_file
// this executable links -- reaches them from rename_char() and
// build_player_index()'s permanently-disabled auto-delete branch
// respectively, neither on the converter's own build_player_index ->
// load_char/store_to_char/save_char call graph; the same held already for
// build_default_account_backed_object_data (only
// load_object_save_data_for_character() calls it, never called here). See
// this file's git history, pre-PS-Task-3, for the three stubs' prior text.
// Linking obj_files.cpp for the first time also surfaced one real gap:
// Crash_get_file_by_name() (unreachable here the same way, but a genuinely
// new undefined symbol, not a stub-shaped one) referenced db_boot.cpp's
// global `buf` scratch array, which this executable deliberately does not
// link. Fixed at the source (obj_files.cpp), not stubbed here -- see that
// file's own PS-Task-3 comment.
//
// persist-split PS Task 4 (world_room_vnum/add_exploit_record inversion +
// rots_persist stand-up) deletes eleven more stubs across two mechanisms.
// Inversion (two symbols): db_players.cpp's save_char()/rename_char() no
// longer call world_room_vnum()/add_exploit_record() directly at all -- both
// call sites now go through persist_hooks.h's pre-boot-registered hooks
// (rots::persist::dispatch_room_vnum/dispatch_exploit_capture, defined in
// db_players.cpp itself). rots_convert never calls
// register_room_vnum_hook()/register_exploit_capture_hook() (both
// app-layer/comm.cpp-only boot steps, mirroring entity_hooks.h's pattern),
// so the hooks' null defaults fire instead -- and those null defaults
// (tripwire log + NOWHERE; tripwire no-op) are byte-identical to this file's
// two deleted stand-ins, for the exact same reachability reasons those
// stand-ins' own comments already argued (save_char()'s branch is provably
// unreachable here per convert_main.cpp's load-room checkpoint; rename_char()
// is never called by this executable's load/store/save flow at all).
// Controller-adjudicated relocation (nine symbols, verbatim, each argued
// leaf-clean at its new home -- see that file's own per-symbol comment):
// find_player_in_table()/find_name()/unaccent() move into db_players.cpp;
// recalc_skills() and utils::set_tactics()/set_shooting()/set_casting()
// move into entity_lifecycle.cpp; file_to_string()/file_to_string_alloc()
// move into db_players.cpp from db_boot.cpp. recalc_skills() in particular
// used to carry a SIMPLIFIED hand-duplicated stand-in here (language-only,
// omitting the ch->knowledge[] recomputation) rather than a tripwire -- this
// executable now runs the real body; ConvertEquivalence 17/17 after this
// change is the proof that ch->knowledge[] (a runtime-only derived field,
// never in char_file_u/char_to_store's output) stays output-invisible.
// This task also adds one library MEMBERSHIP (not a stub deletion, since
// color_convert.cpp never had a stub here): color_convert.cpp -- see the
// "nearest_ansi_color()/convert_old_colormask()" comment below, now updated
// for its new home. See this file's git history, pre-PS-Task-4, for the
// full prior stub text.

#include "base_utils.h"
#include "char_utils.h"
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

// buf/buf1 (db_boot.cpp globals) -- DELETED (entity-seed Task 5). save_char()
// and rename_char() (db_players.cpp) composed through `buf`, and
// write_exploits() (db_players.cpp) also used `buf` -- all three now compose
// locally via std::string/std::format (mirroring the std::format style each
// function already used elsewhere), so this executable no longer references
// either scratch buffer at all.

// nearest_ansi_color()/convert_old_colormask()/
// sync_color_slot_foreground_from_ansi() + the color_color[]/num_of_colors
// table pair are DELETED (persist-split PS Task 1): color.cpp's pure
// conversion helpers (no comm/game dependency) now live in the new
// color_convert.cpp leaf TU (see this file's git history, pre-PS-Task-1,
// for the deleted stand-ins' prior text). color_convert.cpp itself
// originally joined ROTS_SERVER_SOURCES and rots_convert's own direct
// source list; persist-split PS Task 4 moved it again, into
// ROTS_PERSIST_SOURCES (rots_persist, membership not relocation -- it was
// already leaf-clean), since it exists to serve persistence conversion.
// ageland and rots_convert both now link the one real definition of each
// symbol through RotS::persist instead. character_json.cpp's
// truecolor-setting codec and db_players.cpp's load_char()/
// load_char_from_text() still call the real nearest_ansi_color()/
// convert_old_colormask() exactly as before -- only their home TU changed,
// twice.

// The persisted-stat affect/derived-ability engine's remaining support
// stubs -- get_from_affected_type_pool()/put_to_affected_type_pool(),
// pool_to_list()/from_list_to_pool() + affected_list/affected_list_pool/
// universal_list_counter/used_in_universal_list, class_HP(), and
// get_current_time_phase() -- are DELETED (entity-seed Task 5): all five
// symbols (plus int pulse's definition) are now real entity_lifecycle.cpp
// definitions, so rots_convert and ageland link the one real definition of
// each instead of a second hand-duplicated copy. See entity_lifecycle.cpp's
// "Entity-tier leaf helpers" section instead.

// get_race_weight()/get_race_height() are DELETED (entity-seed Task 5):
// both are now real entity_lifecycle.cpp definitions (see that file's
// "Entity-tier leaf helpers" section) instead of hand-duplicated copies.

// char_control_array/char_exists()/set_char_exists()/remove_char_exists()
// are DELETED (entity-seed Task 5): all four are now real
// entity_lifecycle.cpp definitions (see that file's "Entity-tier leaf
// helpers" section) instead of a duplicated bit array.

// clear_account_backed_object_bytes_for_character() is DELETED (entity-seed
// Task 5): free_char() (entity_lifecycle.cpp) no longer calls it directly --
// it dispatches through entity_hooks.h's char-teardown hook instead, and
// this executable never calls register_char_teardown_hook() (an
// app-layer/comm.cpp-only boot step), so the hook's null default fires. That
// default is a provable no-op for the same reason this stub was: the
// staged-object map it would erase from can only gain entries via
// interpre.cpp's login flow, never on this executable's call graph.

// ===========================================================================
// Unreachable stubs -- each of these is DEFINED in a linked TU (handler.cpp:
// fname()/other_side(), app layer, not linked here) that this executable's
// own call graph never invokes (convert_main.cpp calls only
// build_player_index()/load_char()/store_to_char()/save_char() -- see that
// file; fname()'s only reachable-in-principle caller is
// utils::get_object_name(), other_side()'s is utils::is_hostile_to(),
// neither on that call graph), but whose references still need to resolve
// for the whole TU to link. Each logs loudly if ever actually reached, since
// "never called" is exactly the kind of claim worth a tripwire rather than
// silent (potentially wrong) success. (Persist-split PS Task 4: this
// section used to also cover unaccent()/find_name()/find_player_in_table()
// -- all three deleted below, now real db_players.cpp definitions.)
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

// isname_nullable() is DELETED (entity-seed Task 5): it is now a real
// entity_lifecycle.cpp definition (see that file's "Entity-tier leaf
// helpers" section) instead of a stub.

// unaccent()/find_name()/find_player_in_table() are DELETED (persist-split
// PS Task 4, controller-adjudicated relocation): all three are now real
// db_players.cpp definitions (relocated verbatim from utility.cpp/
// interpre.cpp -- see db_players.cpp's own comments at each), so this
// executable links the one real definition of each through
// db_players.cpp -- already directly compiled here -- instead of a
// tripwire-logged stand-in.

// set_title() is DELETED (entity-seed Task 5): it is now a real
// entity_lifecycle.cpp definition (see that file's "Entity-tier leaf
// helpers" section) instead of a stub.

// ===========================================================================
// send_to_char() (both overloads) / vsend_to_char() / act() /
// track_specialized_mage() / untrack_specialized_mage() -- comm.cpp.
// DELETED (entity-seed Task 3, spec Sec13 pattern): these six stubs are
// superseded by output_seam.cpp, which now lives in rots_core (linked here
// via RotS::core) and defines all five global symbols (vsend_to_char calls
// through send_to_char, so it needs no sink of its own) as forwarders
// through a null-defaulted rots::output::Sinks aggregate. rots_convert never
// calls register_game_output_sinks() (that registration is comm.cpp/
// run_the_game()-only, an app-layer boot step this executable does not
// run), so every one of these symbols falls back to output_seam.cpp's
// tripwire-logged no-op default here -- byte-for-byte the same semantics
// this file's hand-duplicated stubs used to provide (log via
// rots::log::write_stderr, then return without touching any state), just
// with one real definition instead of two. See git history (pre-Task-3) for
// the prior stub text and the per-symbol reachability analysis (why each
// was safe to stub: send_to_char/vsend_to_char/act are unreachable from the
// converter's load/store/save flow; track_specialized_mage/
// untrack_specialized_mage ARE genuinely reachable via
// utils::set_specialization(), but are a safe no-op because
// specialized_mages is in-memory-only live-broadcast bookkeeping, never
// persisted).
// ===========================================================================

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

// player_spec::weapon_master_handler's ctor/get_attack_speed_multiplier()
// stub is DELETED (entity-seed Task 5): entity_lifecycle.cpp's
// recalc_abilities() no longer constructs this class directly -- it
// dispatches through entity_hooks.h's attack-speed-multiplier hook instead,
// and this executable never calls register_attack_speed_multiplier_hook()
// (an app-layer/comm.cpp-only boot step), so the hook's null default (1.0f)
// fires. Byte-identical to this stub's old return value, for the same
// reason this stub was safe: this executable's ch->equipment[] is always
// null (see convert_main.cpp), so recalc_abilities()'s weapon branch never
// runs here either way.

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

