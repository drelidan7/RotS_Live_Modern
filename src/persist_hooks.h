#pragma once
// persist_hooks.h -- dependency-inversion seam for db_players.cpp's last two
// upward edges onto db_world.cpp/db_boot.cpp (persist-split PS Task 4,
// mirroring entity_hooks.h/output_seam.h's spec Sec13 pattern). See each
// hook's comment below for the call site it replaces and why a null default
// is safe until registration runs. Backing storage + dispatch helpers are
// defined in db_players.cpp itself (not a separate .cpp, matching
// entity_hooks.h's precedent of keeping them beside the sole caller) -- see
// that file's "persist_hooks.h dispatch" section.

struct char_data;

namespace rots::persist {

// save_char()'s load-room fallback: `if ((load_room == NOWHERE) &&
// (ch->in_room != NOWHERE)) load_room = <hook>(ch->in_room);`. db_world.cpp
// registers the real world_room_vnum() (world[room_index].number) at boot,
// before boot_db() runs; null default is a tripwire log + NOWHERE, byte-
// identical to rots_convert's now-deleted convert_stubs.cpp world_room_vnum()
// stub (that stub was already proven unreachable there -- see
// convert_main.cpp's load-room checkpoint comment -- so the hook's null
// default only ever needs to reproduce that same "unreachable" shape, not a
// real room lookup).
using room_vnum_fn = int (*)(int room_index);
void set_room_vnum_hook(room_vnum_fn hook);

// rename_char()'s exploit-trail note: `add_exploit_record(EXPLOIT_ACHIEVEMENT,
// ch, 0, namebuf)`. db_boot.cpp registers the real add_exploit_record()
// (its live capture-not-codec implementation) at boot, before boot_db() runs;
// null default is a loud tripwire no-op, matching rots_convert's now-deleted
// convert_stubs.cpp add_exploit_record() stub (rename_char() is unreachable
// in that executable's load/store/save flow, so the stub never actually
// fired there either).
using exploit_capture_fn = void (*)(int record_type, char_data* victim, int int_param, const char* extra);
void set_exploit_capture_hook(exploit_capture_fn hook);

// Dispatch entry point for the exploit-capture hook above. Defined in
// db_players.cpp, given external linkage there (unlike its file-local
// dispatch_room_vnum() sibling) specifically so fight.cpp's
// add_exploit_record() call sites can route through it once fight.cpp joins
// rots_combat (combat-pilot wave Task 5, pilot-census.md section 3.11) --
// the same "dispatch declared here, called from a DIFFERENT TU than the one
// owning the backing storage" convention entity_hooks.h already established
// for dispatch_target_valid()/dispatch_character_died().
void dispatch_exploit_capture(int record_type, char_data* victim, int int_param, const char* extra);

}
