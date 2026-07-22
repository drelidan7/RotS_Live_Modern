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
// a new hook header (see the census's section D verdict). These seven had
// no consumer when built (this wave was consumer-free); they existed so a
// future combat-tier TU (mobact/ranger/spec_pro/fight, per census section C)
// could call them exactly like send_to_char, resolving downward instead of
// welding back up into comm.cpp. STATUS UPDATE (combat-pilot wave):
// fight.cpp's own break_spell/abort_delay/complete_delay/
// send_to_room_except_two calls are real consumers now that fight.cpp has
// joined rots_combat -- no call-site edit was needed for them, since they
// resolve to this same symbol name by dependency inversion; mobact/ranger/
// spec_pro remain the still-app future consumers. Same null-safe "logged
// no-op" default as
// the five above for the six void forwarders; the pointer-returning
// txt-block forwarder is the one deliberate taxonomy call documented at its
// own definition in output_seam.cpp.
//
// Plain function pointers, not std::function: these are hot paths (every
// in-game message crosses send_to_char/act), so no per-call type erasure.

#include <string_view>

struct char_data;
struct descriptor_data;
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
// send_to_room_except() (comm.cpp; Cluster B wave Task 1; cb-task-1-brief.md
// Step 4; cb-census.md section 5.5) -- script.cpp:1595-1596's two call
// sites are the up-call this forwarder inverts; comm.cpp itself has no
// internal caller of the plain symbol (same "no comm.cpp-internal caller"
// shape as send_to_all/send_to_room above), so no call-site edit is needed
// anywhere for this seam to take effect once comm.cpp backs it.
using send_to_room_except_fn = void (*)(std::string_view message, int room,
    char_data* excluded_character);
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
// one is for callers above comm.cpp: ranger.cpp/spec_pro.cpp/shop.cpp
// (still app-compiled) call this exact overload today, and so do
// mudlle.cpp/mudlle2.cpp -- promoted to rots_script in the l4-seed wave, so
// their calls now resolve as a legal downward edge, not an app-tier weld --
// each wanting a block pre-populated with a bounded copy of its own text.
// Returns a pointer, unlike the six void forwarders above, whose
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

// put_to_txt_block_pool(struct txt_block*) (comm.cpp) -- the PUT-side
// counterpart of the txt-block-pool getter above (l4-seed wave, Task 1;
// l4-census.md section "put_to_txt_block_pool forwarder"). mudlle.cpp/
// mudlle2.cpp call this exact global symbol to return a txt_block to the
// pool; both files promoted to rots_script in this same wave, so the call
// resolves as a legal downward L4->L1 edge, not an app-tier weld. Void,
// unlike the pointer-returning GET forwarder above: PUT never dereferences
// its argument, so the null-sink default is a SAFE logged no-op (this
// header's dominant "logged no-op" taxonomy, not the GET forwarder's
// abort-tripwire exception) -- a discarded-without-registration txt_block*
// would leak (never returned to the pool), but that is a leak, not a
// crash.
using put_txt_block_to_pool_fn = void (*)(struct txt_block*);

// close_socket(descriptor_data*, int) (comm.cpp; behavior wave Task 1;
// census section 9) -- limits.cpp's future upward close_socket(character->
// desc) call site (limits.cpp:609, consumer-free this task) is the edge
// this forwarder inverts, the same "future combat-tier caller" shape as
// send_to_all/break_spell above. comm.cpp keeps the real body (renamed to
// close_socket_impl, since this seam takes over the plain close_socket
// global symbol the way send_to_char/act/send_to_all etc. already do) --
// descriptor_list/rots_net::close_socket are app-owned session state at
// this seam's call site. Void, so the null-sink default is a SAFE logged
// no-op (this header's dominant taxonomy), not the txt-block-pool GET
// forwarder's abort-tripwire exception.
using close_socket_fn = void (*)(struct descriptor_data* d, int drop_all);

// no_specials read accessor (comm.cpp's no_specials global; census section
// 10) -- mobact.cpp's future read-only reference (mobact.cpp:122,
// `!no_specials`, consumer-free this task). Read accessor, NOT a
// storage-move: the sole writer stays comm.cpp's own boot sequence
// (comm.cpp:516). Bool return, so the null-sink default is a SAFE
// tripwire-logged `false` (specials not suppressed -- the same permissive
// posture as the global's own pre-boot default value), not an abort.
using no_specials_active_fn = bool (*)();

// circle_shutdown WRITE accessor (comm.cpp's circle_shutdown global;
// census section 9) -- limits.cpp's future one write site (limits.cpp:656,
// `circle_shutdown = 1;`, consumer-free this task). Setter forwarder, NOT
// a storage-move -- act_wiz.cpp's own four write sites
// (:1253/:1257x2/:1263/:1269) stay app-tier, unaffected. Void, so the
// null-sink default is a SAFE logged no-op.
using request_circle_shutdown_fn = void (*)();

// msdp_room_update(char_data*) (act_move.cpp; spell-family closure wave
// Task 1; sf-census.md section 4.2) -- mage.cpp's future upward
// msdp_room_update(victim)/msdp_room_update(caster) call sites (mage.cpp:
// 837/969/1156, each behind a local `extern void msdp_room_update(char_data
// *ch);` forward declaration this seam lets T3 delete, consumer-free this
// task) are the edge this forwarder inverts. Unlike this header's other
// entries, the real body is NOT comm.cpp's own: it stays in act_move.cpp
// (renamed to msdp_room_update_impl, the same "takes over the plain global
// symbol" shape as close_socket_impl above), since it composes an MSDP
// room-update packet from world[]/protocol state that is act_move.cpp's,
// not comm.cpp's. comm.cpp still owns the REGISTRATION (register_game_
// output_sinks(), the same pre-boot call site every other sink here uses)
// -- it forward-declares msdp_room_update_impl and installs it as this
// sink, so every existing plain-symbol caller (interpre.cpp:2840/:4150,
// act_wiz.cpp:326, both still app-tier and unaffected by this seam) keeps
// resolving to the identical real body through one extra indirection, the
// same non-breaking-change shape send_to_char's own callers already have.
// Void, so the null-sink default is a SAFE logged no-op (a missed MSDP
// packet, not a crash).
using msdp_room_update_fn = void (*)(char_data* ch);

// descriptor_list HEAD read accessor (comm.cpp's descriptor_list global;
// sf-census.md section 4.1) -- spell_pa.cpp's do_sense_magic() walks
// descriptor_list directly today via a local `extern struct descriptor_data
// *descriptor_list;` forward declaration (spell_pa.cpp:34); once spell_pa.cpp
// promotes to rots_combat that raw extern becomes an illegal upward read of
// comm.cpp's own session storage. Read accessor, NOT a storage-move --
// descriptor_list's sole writer stays comm.cpp's own connection-accept/
// close-socket bookkeeping; this seam only exposes the current head pointer,
// letting a caller replay its existing `for (d = head; d; d = d->next)` walk
// unchanged (descriptor_data's fields are reachable via structs.h from any
// tier). Pointer return, so the null-sink default is a SAFE tripwire-logged
// nullptr (an empty list -- the walk's own loop condition already treats
// that as "no players", not a crash), not an abort.
using descriptor_list_head_fn = descriptor_data* (*)();

struct Sinks {
    send_to_char_fn send_to_char; // comm.cpp's desc-delivery body
    send_to_char_id_fn send_to_char_id; // comm.cpp's descriptor_list-walk body
    act_fn act; // comm.cpp's act_impl body
    mage_roster_fn track_mage; // comm.cpp's track_specialized_mage_impl body
    mage_roster_fn untrack_mage; // comm.cpp's untrack_specialized_mage_impl body
    send_to_all_fn send_to_all; // comm.cpp's send_to_all_impl body
    send_to_room_fn send_to_room; // comm.cpp's send_to_room_impl body
    send_to_room_except_fn send_to_room_except; // comm.cpp's send_to_room_except_impl body
    send_to_room_except_two_fn send_to_room_except_two; // comm.cpp's send_to_room_except_two_impl body
    break_spell_fn break_spell; // comm.cpp's break_spell_impl body
    abort_delay_fn abort_delay; // comm.cpp's abort_delay_impl body
    complete_delay_fn complete_delay; // comm.cpp's complete_delay_impl body
    get_txt_block_from_pool_fn get_txt_block_from_pool; // comm.cpp's get_from_txt_block_pool_impl(string_view) body
    put_txt_block_to_pool_fn put_txt_block_to_pool; // comm.cpp's put_to_txt_block_pool_impl body
    close_socket_fn close_socket; // comm.cpp's close_socket_impl body
    no_specials_active_fn no_specials_active; // comm.cpp's no_specials_active_impl body
    request_circle_shutdown_fn request_circle_shutdown; // comm.cpp's request_circle_shutdown_impl body
    msdp_room_update_fn msdp_room_update; // act_move.cpp's msdp_room_update_impl body
    descriptor_list_head_fn descriptor_list_head; // comm.cpp's descriptor_list_head_impl body
};

// Installs the sinks the game-output forwarders (output_seam.cpp) call
// through. Registered once, before boot -- see comm.cpp's
// register_game_output_sinks()/run_the_game().
void set_sinks(const Sinks& sinks);

} // namespace rots::output
