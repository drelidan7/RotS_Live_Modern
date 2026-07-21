#include <gtest/gtest.h>

#include "../big_brother.h"
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../limits.h"
#include "../mudlle.h"
#include "../pkill.h"
#include "../protocol.h"
#include "../script.h"
#include "../rots_net.h"
#include "../skill_timer.h"
#include "../utils.h"
#include "../warrior_spec_handlers.h"

#if defined(_WIN32)
#include <crtdbg.h>
#include <windows.h>

// Suppress every interactive Windows crash/assert dialog for the test process
// (Phase 3 Task 6). Without this, a test that faults (SEH access violation,
// CRT heap-corruption check, failed assert()) pops a modal "program has stopped
// working" / Debug-Assertion dialog and BLOCKS -- on a headless CI runner that
// hang lasts until ctest's per-test timeout fires (observed as spurious 120s
// "Timeout" results that individually pass locally but each cost two minutes,
// blowing the 30-minute job wall). Routing faults straight to process
// termination + stderr makes a genuine crash fail in milliseconds and report
// as a normal test failure, instead of masquerading as a timeout. No effect on
// a passing test.
static void silence_windows_crash_dialogs()
{
    // No GP-fault message box; fail critical-error prompts (missing media, etc.)
    // rather than prompting; don't let a faulting child re-enable the box.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // Route CRT assert/error/warning reports to stderr instead of a modal dialog.
    for (int report_type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) {
        _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
    }

    // abort() (which failed assertions/terminate call) must not raise the
    // "Debug Error!" dialog either.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}
#endif

#ifdef TESTING
int main(int argc, char* argv[]) {
#if defined(_WIN32)
    silence_windows_crash_dialogs();
#endif
    // Initialize Winsock once for the whole test process (Phase 3 Task 6). The
    // game does this in comm.cpp's main(), but the test harness has its own
    // main() -- without it, every socket()/bind()/connect() in the AcceptPathTest
    // loopback fixtures (startup_options_tests.cpp) fails immediately with
    // WSANOTINITIALISED and rots_net::is_valid_socket() reports the returned
    // INVALID_SOCKET. No-op on POSIX (rots_net::startup() is empty there).
    rots_net::startup();
    // Construct the process-wide skill_timer/big_brother singletons before any
    // test runs (Phase 5 T6, ASan/UBSan sweep). Production reaches these via
    // boot-time skill_timer::create()/big_brother::create() calls (db.cpp) that
    // this test harness's main() never runs; a test that transitively calls
    // fight.cpp's damage() or act_info.cpp's do_affections/do_info without them
    // hits world_singleton<T>::instance() returning `*m_pInstance` while
    // m_pInstance is still null -- a reference-binding-to-null-pointer UB that
    // UBSan flags (previously papered over per-suite by the act_info_format_tests.cpp
    // ensure_skill_timer_created() helper; doing it once here for the whole
    // process closes the gap for every OTHER suite, e.g. mage_tests.cpp's
    // MageProcTest, that also reaches damage() without realizing it needs this).
    // Each create()'s storage is a function-local static, so this is a one-time,
    // idempotent, harness-only bootstrap -- it does not model boot's real
    // weather_info/world wiring and has no effect on shipped game behavior.
    // FIRST-CALL-WINS (singleton.h's world_singleton<T>::create): `static T
    // theInstance(&weather, world)` inside create() is a function-local
    // static, constructed exactly once on the first call that reaches it;
    // every later create() call -- here or in any test that adds its own --
    // just re-points m_pInstance at that same already-constructed instance
    // and silently ignores whatever (weather, world) args it was passed. A
    // future world-booting test that calls create() expecting a *different*
    // room_data*/weather_data& to take effect will get this process's
    // first-ever args instead, with no error or warning.
    game_timer::skill_timer::create(weather_info, nullptr);
    game_rules::big_brother::create(weather_info, nullptr);
    // Installs comm.cpp's real send_to_char/act/track_specialized_mage/
    // untrack_specialized_mage bodies -- plus (blocker-buster wave, census
    // section D) send_to_all/send_to_room/send_to_room_except_two/
    // break_spell/abort_delay/complete_delay/
    // get_from_txt_block_pool(std::string_view) -- as the output seam's
    // sinks (rots::output::set_sinks), once for the whole test process. The
    // game reaches this via run_the_game() (immediately after
    // register_mudlog_broadcast_sink(), before boot_db()), which this test
    // harness's main() never runs; without it, every test that calls
    // send_to_char()/act() -- directly, or transitively through
    // register_mudlog_broadcast_sink()'s own send_to_char() calls -- would
    // silently no-op against output_seam.cpp's tripwire-logged default
    // instead of delivering to a descriptor, exactly the same gap this
    // function already closes for skill_timer/big_brother above; the seven
    // blocker-buster additions ride the same single registration call, so
    // this is a doc-only update, not a new call.
    register_game_output_sinks();
    // entity_hooks.h's four inversion hooks (entity-seed Task 5 + EC Task 2),
    // registered for the same real-body-fidelity reason as the output sinks
    // above: without them this test process would silently exercise the
    // null-hook defaults (dispatch_char_teardown()'s silent no-op;
    // dispatch_attack_speed_multiplier()'s/dispatch_wild_attack_speed_multiplier()'s
    // tripwire-logged 1.0f; dispatch_attacked_player()'s tripwire-logged no-op)
    // instead of obj_files.cpp's/wild_fighting_handler.cpp's/big_brother.cpp's
    // real implementations that ageland registers at boot -- all TUs are
    // already linked into both test binaries, so this only needs the
    // registration calls.
    register_char_teardown_hook();
    register_attack_speed_multiplier_hook();
    register_wild_attack_speed_multiplier_hook();
    register_attacked_player_hook();
    // entity_hooks.h's target-valid/character-died big_brother hook pair
    // (combat-pilot wave Task 2), registered for the same real-body-fidelity
    // reason as the call above: without them this test process would
    // silently exercise dispatch_target_valid()'s/dispatch_character_died()'s
    // tripwire-logged defaults instead of big_brother.cpp's real
    // is_target_valid()/on_character_died() forwarders that ageland registers
    // at boot -- big_brother.cpp is already linked into both test binaries,
    // so this only needs the registration calls. clerics.cpp's/fight.cpp's
    // real call sites dispatch through this pair for real since T3/T5 (see
    // entity_hooks.h); this call also still covers this wave's own seam
    // tests (big_brother_hooks_tests.cpp), the same bridge-before-traffic
    // posture as combat_hooks.h's register_combat_command_dispatch() below.
    register_target_valid_hook();
    register_character_died_hook();
    // entity_hooks.h's txt-block-pool hook pair (world-seed Task 2
    // adjudication), registered for the same real-body-fidelity reason as the
    // four calls above: without it this test process would silently exercise
    // dispatch_get_txt_block_from_pool()'s/dispatch_put_txt_block_to_pool()'s
    // abort()-on-unregistered null default instead of comm.cpp's real pool
    // that ageland registers at boot -- comm.cpp is already linked into both
    // test binaries, so this only needs the registration call.
    register_txt_block_pool_hooks();
    // persist_hooks.h's two inversion hooks (persist-split PS Task 4), registered
    // for the same real-body-fidelity reason as the two calls above: without them
    // this test process would silently exercise the null-hook defaults
    // (dispatch_room_vnum()'s tripwire-logged NOWHERE; dispatch_exploit_capture()'s
    // tripwire-logged no-op) instead of db_world.cpp's/db_boot.cpp's real
    // implementations that ageland registers at boot -- both TUs are already linked
    // into both test binaries, so this only needs the registration calls.
    register_room_vnum_hook();
    register_exploit_capture_hook();
    // world_hooks.h's three inversion hooks (world-seed Task 3),
    // registered for the same real-body-fidelity reason as the two calls
    // above: without them this test process would silently exercise
    // dispatch_boot_the_shops()'s/dispatch_mudlle_converter()'s
    // tripwire-logged null defaults, and dispatch_weather_msdp_update()'s
    // silent no-op default, instead of shop.cpp's/mudlle.cpp's/
    // protocol.cpp's real implementations that ageland registers at boot
    // -- all three TUs are already linked into both test binaries, so
    // this only needs the registration calls.
    register_boot_shops_hook();
    register_mudlle_converter_hook();
    register_weather_msdp_hook();
    // world_hooks.h's send-to-sector/send-to-outdoor hook pair (world-seed
    // Task 5, STOP-adjudicated cascade), registered for the same
    // real-body-fidelity reason as the three calls above: without it this
    // test process would silently exercise
    // dispatch_send_to_sector()'s/dispatch_send_to_outdoor()'s silent
    // no-op defaults instead of comm.cpp's real implementations that
    // ageland registers at boot -- comm.cpp is already linked into both
    // test binaries, so this only needs the registration call.
    register_world_broadcast_hooks();
    // entity_hooks.h's four world-resolver hooks (placement-seam Task 1),
    // registered for the same real-body-fidelity reason as the calls above:
    // without it this test process would silently exercise
    // room_by_id()'s/room_by_id_total()'s/zone_by_id()'s/obj_index_by_id()'s
    // tripwire-abort default instead of db_world.cpp's/zone_load.cpp's real
    // resolvers that ageland registers at boot -- both TUs are already
    // linked into both test binaries, so this only needs the registration
    // call.
    register_world_resolver_hooks();
    // entity_hooks.h's poison-removal notification hook (blocker-buster
    // wave Task 3), registered for the same real-body-fidelity reason as
    // the calls above: without it this test process would silently
    // exercise the tripwire-logged no-op default instead of fight.cpp's
    // real damage()/raw_kill()-backed implementation that ageland registers
    // at boot -- fight.cpp is already linked into both test binaries, so
    // this only needs the registration call.
    register_poison_removal_hook();
    // combat_hooks.h's boot-registered command-dispatch table (blocker-
    // buster wave Task 2), registered for the same real-body-fidelity reason
    // as the calls above: without it, a test that calls
    // rots::combat::issue_command() would silently exercise the tripwire-
    // logged no-op default instead of the real do_hit()/do_flee()/etc.
    // ACMD bodies ageland registers at boot -- interpre.cpp (which defines
    // register_combat_command_dispatch()) is already linked into both test
    // binaries, so this only needs the registration call. fight.cpp's/
    // clerics.cpp's real do_flee/do_stand/special() call sites dispatch
    // through this table for real since combat-pilot T5 (see
    // combat_hooks.h); this call also still covers this wave's own seam
    // tests (combat_hooks_tests.cpp), the same bridge-before-traffic
    // posture as every other hook this file registers.
    register_combat_command_dispatch();
    // entity_hooks.h's extract_char hook (RE-HOMED from combat_hooks.h,
    // l4-seed wave Task 1; originally landed combat-pilot wave Task 4b),
    // registered for the same real-body-fidelity reason as the calls above:
    // without it this test process would silently exercise
    // rots::entity::extract_char()'s tripwire-logged no-op default instead of
    // handler.cpp's real extract_char(ch, new_room) body that ageland
    // registers at boot -- handler.cpp is already linked into both test
    // binaries, so this only needs the registration call. fight.cpp's real
    // call sites dispatch through this hook for real since combat-pilot T5
    // (see entity_hooks.h); this call also still covers this wave's own
    // seam tests (entity_lifecycle_tests.cpp), the same bridge-before-traffic
    // posture as every other hook this file registers.
    register_extract_char_hook();
    // combat_hooks.h's gain_exp/gain_exp_regardless/remove_fame_war_bonuses
    // hooks (combat-pilot wave Task 4b), registered for the same
    // real-body-fidelity reason as the calls above: without them this test
    // process would silently exercise their tripwire-logged no-op defaults
    // instead of limits.cpp's real bodies that ageland registers at boot --
    // limits.cpp is already linked into both test binaries, so this only
    // needs the registration calls. fight.cpp's/clerics.cpp's real call
    // sites dispatch through these hooks for real since combat-pilot T5
    // (see combat_hooks.h); these calls also still cover this wave's own
    // seam tests (combat_hooks_tests.cpp), the same bridge-before-traffic
    // posture as every other hook this file registers.
    register_gain_exp_hook();
    register_gain_exp_regardless_hook();
    register_remove_fame_war_bonuses_hook();
    // combat_hooks.h's app-other trio hooks (combat-pilot wave Task 4b):
    // crash_crashsave/call_trigger/pkill_create, registered for the same
    // real-body-fidelity reason as the calls above: without them this test
    // process would silently exercise their tripwire-logged defaults --
    // including call_trigger()'s MANDATORY TRUE default, see
    // combat_hooks.h's call_trigger_fn comment -- instead of objsave.cpp's/
    // script.cpp's/pkill.cpp's real bodies that ageland registers at boot --
    // all three TUs are already linked into both test binaries, so this only
    // needs the registration calls. fight.cpp's real call sites dispatch
    // through these hooks for real since combat-pilot T5 (see
    // combat_hooks.h); these calls also still cover this wave's own seam
    // tests (combat_hooks_tests.cpp), the same bridge-before-traffic
    // posture as every other hook this file registers.
    register_crash_crashsave_hook();
    register_call_trigger_hook();
    register_pkill_create_hook();
    // script_hooks.h's command-interpreter/PERS hooks (l4-seed wave, Task 1),
    // registered for the same real-body-fidelity reason as the calls above:
    // without them this test process would silently exercise their
    // tripwire-logged/abort-tripwire defaults instead of interpre.cpp's/
    // utility.cpp's real bodies that ageland registers at boot -- both TUs
    // are already linked into both test binaries, so this only needs the
    // registration calls. Consumer-free this task (no mudlle.cpp/mudlle2.cpp
    // call site converts yet); these calls also cover this wave's own seam
    // tests (script_hooks_tests.cpp), the same bridge-before-traffic posture
    // as every other hook this file registers.
    register_command_interpreter_hook();
    register_pers_hook();
    // world_hooks.h's do-wear/is-zone-populated/equip-char/pkill-fame hooks
    // (l4-seed wave, Task 1), registered for the same real-body-fidelity
    // reason as the calls above: without them this test process would
    // silently exercise their tripwire-logged/safe-sentinel defaults
    // instead of act_obj2.cpp's/comm.cpp's/fight.cpp's/pkill.cpp's real
    // bodies that ageland registers at boot -- all four TUs are already
    // linked into both test binaries, so this only needs the registration
    // calls. Consumer-free this task (zone.cpp's own call sites don't
    // convert yet); these calls also cover this wave's own seam tests
    // (world_hooks_tests.cpp), the same bridge-before-traffic posture as
    // every other hook this file registers.
    register_do_wear_hook();
    register_is_zone_populated_hook();
    register_equip_char_hook();
    register_pkill_fame_hooks();
    // entity_hooks.h's char-from-room hook + big_brother's AFK/
    // corpse-decayed pair (behavior wave Task 1), registered for the same
    // real-body-fidelity reason as the calls above: without them this test
    // process would silently exercise their tripwire-logged no-op defaults
    // instead of handler.cpp's/big_brother.cpp's real bodies that ageland
    // registers at boot -- both TUs are already linked into both test
    // binaries, so this only needs the registration calls. Consumer-free
    // this task (limits.cpp's own call sites don't convert yet); these
    // calls also cover this wave's own seam tests
    // (entity_lifecycle_tests.cpp/big_brother_hooks_tests.cpp), the same
    // bridge-before-traffic posture as every other hook this file
    // registers.
    register_char_from_room_hook();
    register_character_afked_hook();
    register_corpse_decayed_hook();
    // combat_hooks.h's one_mobile_activity hook + Crash_idlesave/
    // Crash_extract_objs sibling pair (behavior wave Task 1), registered
    // for the same real-body-fidelity reason as the calls above:
    // mobact.cpp/objsave.cpp are already linked into both test binaries,
    // so this only needs the registration calls. Consumer-free this task;
    // mobact.cpp has no dedicated header, so its registrar is
    // forward-declared locally here (mirroring comm.cpp's own local
    // declaration inside run_the_game()).
    void register_one_mobile_activity_hook();
    register_one_mobile_activity_hook();
    register_crash_idlesave_hook();
    register_crash_extract_objs_hook();
    // script_hooks.h's virt_program_number cell (behavior wave Task 1;
    // CONTROLLER ADDENDUM item 3), registered for the same
    // real-body-fidelity reason as the calls above: spec_ass.cpp is
    // already linked into both test binaries, so this only needs the
    // registration call. Consumer-free this task; this call also covers
    // this wave's own seam test (script_hooks_tests.cpp).
    register_virt_program_number_hook();
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    rots_net::shutdown();
    return result;
}
#endif
