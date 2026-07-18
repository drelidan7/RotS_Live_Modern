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
// no-op: exactly rots_convert's historical stub semantics for these five
// symbols (see convert_stubs.cpp's now-deleted "send_to_char() (both
// overloads) / vsend_to_char() / act()" and "track_specialized_mage() /
// untrack_specialized_mage()" sections).
//
// Plain function pointers, not std::function: these are hot paths (every
// in-game message crosses send_to_char/act), so no per-call type erasure.

#include <string_view>

struct char_data;
struct obj_data;

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

struct Sinks {
    send_to_char_fn send_to_char; // comm.cpp's desc-delivery body
    send_to_char_id_fn send_to_char_id; // comm.cpp's descriptor_list-walk body
    act_fn act; // comm.cpp's act_impl body
    mage_roster_fn track_mage; // comm.cpp's track_specialized_mage_impl body
    mage_roster_fn untrack_mage; // comm.cpp's untrack_specialized_mage_impl body
};

// Installs the sinks the game-output forwarders (output_seam.cpp) call
// through. Registered once, before boot -- see comm.cpp's
// register_game_output_sinks()/run_the_game().
void set_sinks(const Sinks& sinks);

} // namespace rots::output
