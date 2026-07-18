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
// rots_persist stand-up) deletes eight stubs across two mechanisms.
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
// never in char_file_u/char_to_store's output) stays output-invisible. Of
// these nine relocated symbols, only six actually deleted a stub/stand-in
// here (find_player_in_table/find_name/unaccent/recalc_skills/
// file_to_string/file_to_string_alloc, folded into the eight-stub count
// above with the two inversions); utils::set_tactics()/set_shooting()/
// set_casting() never had one -- char_utils.cpp was already a direct
// rots_convert source before and after the move, so relocating them within
// already-linked TUs closed no converter-side weld.
// This task also adds one library MEMBERSHIP (not a stub deletion, since
// color_convert.cpp never had a stub here -- persist-split PS Task 1 had
// already deleted its stand-in when the leaf TU was carved): color_convert.cpp
// -- see the "nearest_ansi_color()/convert_old_colormask()" comment below,
// now updated for its new home. See this file's git history, pre-PS-Task-4,
// for the full prior stub text.

// entity-completion Task 1 (spec Sec10 step 4, relocation instrument)
// deletes three more stubs the same way: fname()/other_side()
// (formerly handler.cpp) and get_hit_text() (formerly fight.cpp, with its
// attack_hit_text[] table) are now real char_utils.cpp/consts.cpp
// definitions respectively -- char_utils.cpp is already a direct
// rots_convert source, and consts.cpp joined rots_core in entity-seed
// Task 2, so this executable links the one real definition of each
// instead of a tripwire-logged stand-in. other_side_num() (handler.cpp,
// relocated alongside other_side() -- same macro-logic family) never had
// a stub here: no rots_convert-linked TU calls it. See this file's git
// history, pre-entity-completion-Task-1, for the three deleted stubs' prior
// text.

// EC Task 2 (wild-fighting attack-speed / big-brother PK-notification hook
// inversion) deletes the last remaining stub body in this file:
// player_spec::wild_fighting_handler's ctor/get_attack_speed_multiplier()
// (see this file's "player_spec::wild_fighting_handler" section, below,
// for the full reachability argument). This file now has ZERO stub bodies
// left -- every symbol rots_convert needs is either a real cross-linked
// definition or a null-defaulted entity_hooks.h/persist_hooks.h/
// output_seam.h hook default. Task 3 deletes this now-empty ledger file
// outright.

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

// fname()/other_side() stubs are DELETED (entity-completion Task 1):
// both are now real char_utils.cpp definitions (relocated verbatim from
// handler.cpp -- see that file's "fname()/other_side()/other_side_num()
// relocated verbatim" comments), and char_utils.cpp is already a direct
// rots_convert source, so this executable links the one real definition
// of each instead of a tripwire-logged stand-in. (Persist-split PS
// Task 4: this section used to also cover unaccent()/find_name()/
// find_player_in_table() -- all three deleted earlier, now real
// db_players.cpp definitions.)

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

// player_spec::wild_fighting_handler's ctor/get_attack_speed_multiplier()
// stub is DELETED (EC Task 2): char_utils.cpp's get_energy_regen() no longer
// constructs this class directly -- it dispatches through entity_hooks.h's
// wild-attack-speed-multiplier hook instead, and this executable never calls
// register_wild_attack_speed_multiplier_hook() (an app-layer/comm.cpp-only
// boot step), so the hook's null default (1.0f) fires. Byte-identical to
// this stub's old return value, for the same reachability reason this stub
// was safe: get_energy_regen() is not on the load_char()/store_to_char()/
// save_char() call graph (nothing in db_players.cpp/entity_lifecycle.cpp/
// char_utils.cpp itself calls it -- it is a live-combat energy-regen-rate
// query). This was the last stub body in this file: every remaining section
// below is a DELETED-stub marker comment (or, further up, still-live
// per-symbol reachability history) -- rots_convert now links a real
// definition, or a null-defaulted hook default, for every symbol it needs.
// Task 3 deletes this now-empty ledger file outright.

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

// get_hit_text() stub is DELETED (entity-completion Task 1): the real
// definition (relocated verbatim from fight.cpp, alongside its
// attack_hit_text[] table) now lives in consts.cpp, joined into
// rots_core, so this executable links the one real definition through
// RotS::core instead of a tripwire-logged placeholder. Follow-on note
// (get_damage_report() itself still living in char_utils.cpp, a direct
// rots_convert source) carries forward unchanged.

