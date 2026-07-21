#pragma once
// world_hooks.h -- dependency-inversion seam for db_world.cpp's and
// weather.cpp's last upward edges into command-tier/app-tier TUs
// (spec Sec13 pattern, mirroring entity_hooks.h/persist_hooks.h). See each
// hook's comment below for the call site it replaces and why its null
// default is safe, deliberately loud, or (mudlle-converter, below) a
// documented tripwire whose return value must not be used, until
// registration runs. Backing storage + dispatch helpers are defined in the
// dispatching TU itself (db_world.cpp for the first two hooks, weather.cpp
// for the third) -- mirroring entity_hooks.h's precedent of keeping them
// beside the sole caller, not a separate world_hooks.cpp -- since none of
// the three hooks below are dispatched cross-TU (unlike entity_hooks.h's
// txt-block-pool pair, which needed a shared header declaration for that
// reason).

#include <cstdio>
#include <string_view>

struct char_data;
struct obj_data;

namespace rots::world {

// index_boot()'s DB_BOOT_SHP case (db_world.cpp): `boot_the_shops(db_file,
// file_path.data())`. shop.cpp registers the real boot_the_shops() at boot,
// before boot_db() runs; null default is a loud tripwire log + no-op --
// ageland always registers pre-boot, so a missing registration would boot
// without shops, and the tripwire makes that unmissable instead of a
// silently empty shop_index.
using boot_shops_fn = void (*)(FILE* shop_f, char* filename);
void set_boot_shops_hook(boot_shops_fn hook);

// boot_mudlle()'s per-program conversion (db_world.cpp): `mobile_program[i]
// = mudlle_converter(mobile_program[i])`. mudlle.cpp registers the real
// mudlle_converter() at boot, before boot_db() runs; null default is a
// tripwire log + returning the input pointer unchanged. That return value
// is NOT safe to use afterward (a prior wording claimed it was): the
// actual caller, boot_mudlle(), does `tmpstr = mobile_program[i];
// mobile_program[i] = dispatch_mudlle_converter(mobile_program[i]);
// RELEASE(tmpstr);` -- if the hook is unregistered, dispatch returns the
// same pointer tmpstr already holds, so RELEASE(tmpstr) frees the buffer
// mobile_program[i] now (again) points at, leaving it dangling. The null
// default exists for deterministic, LOUD unregistered behavior (log +
// identity return), NOT safety: RELEASE(tmpstr) runs unconditionally
// either way, so no return value here could avoid the dangle -- this
// default's only job is to make an unregistered call path unmissable
// (log) rather than silent, not to protect mobile_program[i]. It is
// unreachable in every shipped binary: run_the_game() always registers
// the real converter before boot_db() runs.
using mudlle_converter_fn = char* (*)(char* source);
void set_mudlle_converter_hook(mudlle_converter_fn hook);

// weather.cpp's two MSDP broadcast sites (another_hour()'s eMSDP_WORLD_TIME
// push; weather_change()'s eMDSP_WEATHER push) used to call weather.cpp's
// own send_msdp_function() dispatcher directly, each with a different
// lambda; `kind` now selects which of the two behaviors to run.
// protocol.cpp registers broadcast_weather_msdp_update() -- the former
// send_msdp_function() dispatcher body merged with both lambda bodies,
// relocated verbatim -- at boot, before boot_db(); see world_hooks.h.
// Null default is a SILENT no-op, unlike this header's other two tripwire
// defaults: this is a pure best-effort notification push (not state), and
// a test process that never registers protocol.cpp's sink must not spam
// stderr on every weather/time tick -- mirroring entity_hooks.h's
// char-teardown hook precedent (a provable silent no-op) rather than this
// header's other two hooks' tripwires.
enum class weather_msdp_kind { world_time,
    weather };
using weather_msdp_update_fn = void (*)(weather_msdp_kind kind);
void set_weather_msdp_update_hook(weather_msdp_update_fn hook);

// weather.cpp's broadcast sites (weather_message()'s/weather_change()'s
// per-sector weather text; check_sun_change()'s day/night announcements;
// another_hour()'s moon-rise/moon-set announcements) used to call
// comm.cpp's send_to_sector()/send_to_outdoor() directly -- an upward
// edge into descriptor_list (app-owned session data), surfaced by
// rots_world_linkcheck (world-seed Task 5, STOP-adjudicated cascade).
// comm.cpp registers both real functions (register_world_broadcast_hooks())
// at boot, before boot_db(), alongside this header's other three
// registrations; see comm.h/comm.cpp. Unlike the boot-shops/mudlle-converter
// hooks above, the real send_to_sector()/send_to_outdoor() bodies are NOT
// relocated -- both walk descriptor_list, upper-tier session data this
// library must not reach -- only the call from weather.cpp is inverted.
// Null default is a SILENT no-op, the same class as this header's
// weather-MSDP hook above (not the boot-shops/mudlle-converter tripwires):
// both are pure best-effort player-notification pushes (not state), and a
// test process that never registers comm.cpp's sinks must not spam stderr
// on every weather/time tick.
using send_to_sector_fn = void (*)(std::string_view message, int sector_type);
void set_send_to_sector_hook(send_to_sector_fn hook);

using send_to_outdoor_fn = void (*)(std::string_view message, int mode);
void set_send_to_outdoor_hook(send_to_outdoor_fn hook);

// zone.cpp:598's do_wear() call (`do_wear(mob, mutable_arg("all"), 0, 0, 0);`,
// zone reset's ZONE_MOB_UNWEAR-adjacent case; l4-seed wave Task 1;
// l4-census.md section 3.5). Takes only the opaque char_data* -- the "all"
// argument and zero cmd/subcmd are fixed literals at the one call site, so
// the real body (act_obj2.cpp's do_wear_all_items() wrapper) bakes them in
// rather than widening this hook to the full 5-arg ACMD shape.
// act_obj2.cpp registers the real wrapper at boot, before boot_db() runs;
// null default is a loud tripwire log + no-op (a state-change command a
// missed hook would silently skip, matching this header's boot-shops/
// mudlle-converter tripwire class, not the weather hooks' silent one).
using do_wear_fn = void (*)(char_data* mob);
void set_do_wear_hook(do_wear_fn hook);
void dispatch_do_wear(char_data* mob);

// zone.cpp's is_empty(int zone_nr) (reset-gating query, `is_empty(i) &&
// zone_table[i].age >= ...`; l4-census.md section 3.5) walks
// descriptor_list -- app-owned session state this library must not reach.
// This hook is the semantic INVERSE (returns true when the zone HAS a
// player, matching a more legible name than "is_empty"), so a future
// zone.cpp call site reads `!dispatch_is_zone_populated(i)` where it used
// to read `is_empty(i)`. comm.cpp registers the real body (which owns
// descriptor_list) at boot, before boot_db() runs. Null default is TRUE
// ("assume populated") -- the SAFE-SENTINEL class (like persist_hooks.h's
// room_vnum_fn returning NOWHERE), not a tripwire: zone.cpp's only use of
// this query gates a destructive zone reset, and defaulting to "populated"
// means an unregistered hook degrades to "never reset" rather than
// risking a reset while players are actually present.
using is_zone_populated_fn = bool (*)(int zone_nr);
void set_is_zone_populated_hook(is_zone_populated_fn hook);
bool dispatch_is_zone_populated(int zone_nr);

// zone.cpp:612's equip_char() call (`equip_char(mob, obj, ZCMD.arg2);`;
// l4-seed wave Task 1 CONTROLLER ADDENDUM item 1; l4-census.md section
// 3.3). OVERTURNED from the spec's original "relocate to rots_entity"
// default: equip_char()'s own body (fight.cpp) has a poison-coupling block
// that calls damage()/raw_kill() (both rots_combat, L3), so it cannot move
// to rots_entity (L2) without creating an illegal upward L2->L3 edge --
// see fight.cpp's own equip_char() comment. equip_char() stays defined in
// fight.cpp; only zone.cpp's call site inverts through this hook.
// fight.cpp registers the real body at boot, before boot_db() runs (a
// legal combat->world DOWNWARD registration, mirroring mudlle_converter_fn's
// registrar shape above). Null default is a loud tripwire log + no-op
// (void-returning state-change command, same class as do_wear_fn above).
using equip_char_fn = void (*)(char_data* character, obj_data* item, int item_slot);
void set_equip_char_hook(equip_char_fn hook);
void dispatch_equip_char(char_data* character, obj_data* item, int item_slot);

// zone.cpp's pkill_get_good_fame()/pkill_get_evil_fame() query pair
// (`pkill_get_good_fame() > pkill_get_evil_fame()`, the DOOR_UNBLOCK-family
// fame-lead gates; l4-seed wave Task 1 CONTROLLER ADDENDUM item 2;
// l4-census.md section 3.6). OVERTURNED from the spec's original
// "relocate to rots_persist" default: pkill.cpp's good_ranking/
// evil_ranking backing globals live in app-tier pkill.cpp itself, not
// pkill_json.cpp (rots_persist) -- relocating just the two accessor
// functions would leave rots_persist depending on unresolved app-tier
// storage, failing PersistLayerAcyclicity. Home is world_hooks.h (not
// persist_hooks.h): the sole consumer is zone.cpp/rots_world, mirroring
// the boot_shops_fn pattern (an app-tier file registering into the
// consumer's own upward seam) rather than routing through rots_persist's
// seam for a TU that isn't itself a rots_persist member. pkill.cpp
// registers both real bodies at boot, before boot_db() runs. Null default
// is 0 for both -- the SAFE-SENTINEL class (no natural NOWHERE-equivalent
// exists for a fame count, so 0 is chosen as the neutral value: with both
// sides defaulting to 0, neither of zone.cpp's `> `/`<=` fame-lead
// comparisons can spuriously favor either side).
using pkill_fame_query_fn = int (*)();
void set_pkill_get_good_fame_hook(pkill_fame_query_fn hook);
void set_pkill_get_evil_fame_hook(pkill_fame_query_fn hook);
int dispatch_pkill_get_good_fame();
int dispatch_pkill_get_evil_fame();

}
