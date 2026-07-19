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
// tripwire log + returning the input pointer unchanged -- the only safe
// placeholder for a function whose whole contract is "return a new
// converted buffer": returning the untouched input at least avoids
// corrupting or leaking mobile_program[i] the way a null or a freshly
// allocated empty buffer would.
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

}
