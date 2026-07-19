#pragma once
// world_hooks.h -- dependency-inversion seam for db_world.cpp's and
// weather.cpp's last upward edges into command-tier/app-tier TUs
// (spec Sec13 pattern, mirroring entity_hooks.h/persist_hooks.h). See each
// hook's comment below for the call site it replaces and why its null
// default is safe (or deliberately loud) until registration runs. Backing
// storage + dispatch helpers are defined in the dispatching TU itself
// (db_world.cpp for the first two hooks, weather.cpp for the third) --
// mirroring entity_hooks.h's precedent of keeping them beside the sole
// caller, not a separate world_hooks.cpp -- since none of the three hooks
// below are dispatched cross-TU (unlike entity_hooks.h's txt-block-pool
// pair, which needed a shared header declaration for that reason).

#include <cstdio>
#include <string_view>

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
// default is not a safe placeholder; it is a deterministic tripwire (log
// + identity return) whose only job is to keep the unregistered path from
// doing something worse than dangling -- and it is unreachable in every
// shipped binary, since run_the_game() always registers the real
// converter before boot_db() runs.
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

}
