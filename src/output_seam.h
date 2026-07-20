#pragma once
// output_seam.h -- dependency-inverted send_to_char/act output sinks (spec
// Sec13 pattern, mirroring rots/platform/log.h's logging seam). comm.cpp is
// the last remaining app-layer weld into the lower-tier link graph:
// send_to_char (both overloads), act, and the specialized-mage roster
// helpers are called from throughout db_players.cpp/entity_lifecycle.cpp/
// char_utils.cpp and friends, but their real bodies (descriptor_list walks,
// connected-state checks, the mage roster vector) are app-layer state that
// only exists once run_the_game() boots.
//
// This header lets lower-tier callers keep calling the five global symbols
// unchanged (comm.h's declarations); output_seam.cpp defines those symbols
// as forwarders through the plain function pointers below, registered once
// by comm.cpp's register_game_output_sinks() -- called from run_the_game(),
// immediately after register_mudlog_broadcast_sink() and before boot_db(),
// so ageland never runs with an unregistered sink. A null sink is a logged
// no-op -- except the txt-pool getter (get_from_txt_block_pool(std::
// string_view)), which aborts instead of returning a null placeholder; see
// its own comment below -- matching rots_convert's historical stub
// semantics for these five symbols (see convert_stubs.cpp's now-deleted
// "send_to_char() (both overloads) / vsend_to_char() / act()" and
// "track_specialized_mage() / untrack_specialized_mage()" sections).
//
// BLOCKER-BUSTER EXTENSION (+7, census section D
// .superpowers/sdd/blocker-census.md): send_to_all/send_to_room/
// send_to_room_except_two, break_spell/abort_delay/complete_delay, and the
// content-carrying get_from_txt_block_pool(std::string_view) overload join
// the five above -- every one of them is, like send_to_char/act, a
// comm.cpp-owned symbol taking only opaque char_data*/std::string_view
// arguments, so the census verdict was to EXTEND this seam rather than open
// a new hook header (see the census's section D verdict). These seven have
// no consumer yet (this wave is consumer-free); they exist so a future
// wave's combat-tier TU (mobact/ranger/spec_pro/fight, per census section C)
// can call them exactly like send_to_char today, resolving downward instead
// of welding back up into comm.cpp. Same null-safe "logged no-op" default as
// the five above for the six void forwarders; the pointer-returning
// txt-block forwarder is the one deliberate taxonomy call documented at its
// own definition in output_seam.cpp.
//
// Plain function pointers, not std::function: these are hot paths (every
// in-game message crosses send_to_char/act), so no per-call type erasure.

#include <string_view>

struct char_data;
struct obj_data;
struct txt_block;

namespace rots::output {

// One sink per output entry point the lower layers call. Null until the app
// layer registers (register_game_output_sinks(), comm.cpp) -- a null sink is
// a logged no-op, matching the converter's historical stub semantics; in
// ageland registration precedes boot_db(), so the default never fires there.
using send_to_char_fn = void (*)(std::string_view message, char_data* character);
using send_to_char_id_fn = void (*)(std::string_view message, int character_id);
using act_fn = void (*)(std::string_view str, int hide_invisible, char_data* ch,
    obj_data* obj, void* vict_obj, int type, char spam_only);
using mage_roster_fn = void (*)(char_data* mage);

// send_to_all()/send_to_room()/send_to_room_except_two() (comm.cpp) -- the
// room/all-player broadcast trio a future combat-tier caller needs
// alongside send_to_char/act above. Same opaque-argument, descriptor-list-
// walk shape as the existing five; comm.cpp keeps the real bodies
// (descriptor_list/world[] are app-owned session/world state at this seam's
// call site).
using send_to_all_fn = void (*)(std::string_view message);
using send_to_room_fn = void (*)(std::string_view message, int room);
using send_to_room_except_two_fn = void (*)(std::string_view message, int room,
    char_data* excluded_first, char_data* excluded_second);

// break_spell()/abort_delay()/complete_delay() (comm.cpp) -- the delayed-
// command teardown/completion trio. Each takes only an opaque char_data*
// (like track_mage/untrack_mage above), so the same forwarding shape
// applies even though their real bodies mutate delayed-command state
// (waiting_list, the interpreter re-entry) instead of descriptor output.
using break_spell_fn = void (*)(char_data* ch);
using abort_delay_fn = void (*)(char_data* wait_ch);
using complete_delay_fn = void (*)(char_data* ch);

// get_from_txt_block_pool(std::string_view) (comm.cpp) -- the content-
// carrying overload of the txt-block pool getter. Distinct from the no-arg
// overload already inverted through entity_hooks.h's get_txt_block_fn pair
// (target_data's pool traffic, world-seed Task 2): that hook exists because
// entity_lifecycle.cpp sits BELOW comm.cpp and needs an empty block; this
// one is for a future combat-tier caller (ranger.cpp/spec_pro.cpp/
// shop.cpp/mudlle.cpp already call this exact overload today, all still
// app-compiled) that wants a block pre-populated with a bounded copy of its
// own text. Returns a pointer, unlike the six void forwarders above, whose
// unregistered default is a safe null-op (this header's own "logged no-op"
// taxonomy): this one instead matches entity_hooks.h's get_txt_block_fn
// twin hook and tripwire-logs THEN ABORTS on an unregistered sink, because
// -- exactly as entity_hooks.h documents for its own pair -- there is no
// safe placeholder txt_block* to return; comm.cpp's own write_to_q() calls
// this overload internally and immediately dereferences the result
// (pnew->next = ...), so a silently-returned null would surface as a
// confusing null-deref far from the real cause instead of failing loudly at
// the hook boundary. See output_seam.cpp's forwarder for the full contrast
// with the six void forwarders above.
using get_txt_block_from_pool_fn = txt_block* (*)(std::string_view line);

struct Sinks {
    send_to_char_fn send_to_char; // comm.cpp's desc-delivery body
    send_to_char_id_fn send_to_char_id; // comm.cpp's descriptor_list-walk body
    act_fn act; // comm.cpp's act_impl body
    mage_roster_fn track_mage; // comm.cpp's track_specialized_mage_impl body
    mage_roster_fn untrack_mage; // comm.cpp's untrack_specialized_mage_impl body
    send_to_all_fn send_to_all; // comm.cpp's send_to_all_impl body
    send_to_room_fn send_to_room; // comm.cpp's send_to_room_impl body
    send_to_room_except_two_fn send_to_room_except_two; // comm.cpp's send_to_room_except_two_impl body
    break_spell_fn break_spell; // comm.cpp's break_spell_impl body
    abort_delay_fn abort_delay; // comm.cpp's abort_delay_impl body
    complete_delay_fn complete_delay; // comm.cpp's complete_delay_impl body
    get_txt_block_from_pool_fn get_txt_block_from_pool; // comm.cpp's get_from_txt_block_pool_impl(string_view) body
};

// Installs the sinks the game-output forwarders (output_seam.cpp) call
// through. Registered once, before boot -- see comm.cpp's
// register_game_output_sinks()/run_the_game().
void set_sinks(const Sinks& sinks);

} // namespace rots::output
