#pragma once
// combat_hooks.h -- boot-registered command-dispatch seam, originally built
// (blocker-buster wave Task 2; plan
// docs/superpowers/plans/2026-07-19-blocker-buster.md; census
// .superpowers/sdd/blocker-census.md section C) for the four combat-row TUs
// that issue player/mob commands upward into the app-command tier:
// mobact.cpp/spec_pro.cpp/ranger.cpp/fight.cpp. Modeled on
// assign_spell_pointers() (spell_pa.cpp/spells.h, entity-seed Task 1): an
// enum-indexed array of ACMD function pointers, null-initialized at static
// init and populated once at boot by register_combat_command_dispatch()
// (interpre.cpp -- see that file for why it, not a TU of this header's own,
// does the populating: interpre.cpp already forward-declares every ACMD
// target below for its own command-interpreter table, the same reason
// spell_pa.cpp -- already visiting every spell_*() body it wires -- was
// assign_spell_pointers()'s home).
//
// STATUS UPDATE (combat-pilot wave, Task 5): fight.cpp -- and clerics.cpp,
// which defines/consumes several of the same cells (do_mental/do_flee) --
// have JOINED rots_combat and their up-calls now go through
// rots::combat::issue_command()/call_special() for real, resolving as
// genuine intra-lib dispatch rather than an unexercised default.
// spec_pro.cpp/ranger.cpp remain app-compiled and still call do_hit()/
// do_flee()/etc. directly (ROTS_SERVER_SOURCES, a legal same-tier call
// today); this seam exists so that when either of those two TUs joins
// rots_combat in a future wave, EACH of its up-calls converts the same way
// fight.cpp's/clerics.cpp's already did, resolving downward through this
// table rather than welding back up into an app-tier symbol the library
// cannot see. STATUS UPDATE (behavior wave, Task 2/Task 3): mobact.cpp's
// twelve do_* up-calls converted onto this table in Task 2 (still
// app-compiled at the time -- a legal app->lib downward call, the same
// shape fight.cpp's pre-Task-5 calls had); Task 3 then moved mobact.cpp
// into ROTS_SCRIPT_SOURCES (L4), one tier ABOVE this library, so those
// twelve calls now resolve L4->L3 downward through this table -- a
// different shape from fight.cpp's/clerics.cpp's genuine INTRA-lib
// dispatch (mobact.cpp never joins rots_combat itself, so its calls stay
// cross-library rather than becoming same-archive address resolution).
//
// TARGET LIST (25 cells -- census-C's own per-TU counts carried off-by-one
// labels, e.g. "13 distinct" over a 12-name list; reconciled here against
// grep-verified real call sites, not mere mentions -- see
// task-2-report.md's "Census-C reconciliation" for the full diff): the
// union of every do_* function mobact.cpp/spec_pro.cpp/ranger.cpp/fight.cpp
// actually CALLS (`name(`, not `ACMD(name)`/`ACMD(name);`), excluding:
//   - do_recover/do_scan -- census listed them for ranger.cpp, but neither
//     has any direct C++ call site anywhere in the tree; both are reachable
//     only through the general command_interpreter's _cmd_info table, a
//     different, already-existing dispatch mechanism this seam does not
//     duplicate.
//   - do_pracreset -- census listed it for spec_pro.cpp, but its one call
//     site is INSIDE spec_pro.cpp itself, calling its own same-TU function --
//     never an upward edge under any future promotion order, since the
//     target moves atomically with its only caller.
//
// CORRECTION (review, post-landing): the initial 24-cell table MISSED
// do_mental -- fight.cpp's per-tick mental-combat auto-attack in the round
// loop (`do_mental(fighter, mutable_arg(""), 0, 0, 0)`) calls it every
// combat pulse for a fighting mental/shadow combatant, and it is
// ACMD in clerics.cpp (a DEFER-11 TU at the time this correction was
// written -- clerics.cpp itself joined rots_combat in the later
// combat-pilot wave's Task 5, so this specific call is now intra-lib), the
// same cross-row class as ambush/cast/hide/trap below. It passed both
// exclusion tests above (a real
// call site, not a same-TU self-call), so the miss was a pure enumeration
// gap in the first pass, not a signature or classification error. A
// follow-up sweep (grep for every `#define` in the four TUs, `&do_*`
// address-of/function-pointer indirection, and COMMANDO-style macro usage;
// then a full `do_*`-identifier inventory across all four files,
// classifying every name not already in this table) found ZERO further
// misses: the ~23 extra names it turned up are each one of (a) a function
// DEFINED in ranger.cpp/spec_pro.cpp/fight.cpp itself with no call site
// anywhere in the four TUs (reachable only via the general command
// interpreter when a player types the command, e.g. do_calm/do_shoot/
// do_track/do_twohand/etc.), (b) a bare `ACMD(name);` forward declaration
// with no call site at all (do_drop/do_get/do_kick/do_pull), (c) a
// same-named goto label inside its own function's body coincidentally
// matching `do_x:` (do_bendtime/do_blinding/do_mark/do_windblast), or (d) a
// plain comment mention (do_title). See task-2-report.md's "Sweep for
// hidden/indirect do_* calls" for the full per-name classification.
//
// Five cells (ambush/cast/hide/mental/trap) are DEFINED in
// ranger.cpp/spell_pa.cpp/clerics.cpp -- other combat-row TUs, not
// app-command tier -- rather than an act_*.cpp file. They were included
// anyway at Task 2 landing time: DEFER-11 promotion order was not fixed, so
// a cross-TU call between two not-yet-promoted combat-row TUs had to route
// through this seam exactly like a genuine app-command call until (and
// unless) both sides of the call promoted together, at which point the
// registered pointer would simply become an intra-lib address instead --
// see task-2-report.md for the per-cell breakdown. STATUS UPDATE
// (combat-pilot wave, Task 5): clerics.cpp itself joined rots_combat, so
// `mental`'s registered pointer is now exactly that intra-lib address, the
// first cell in this five-name group to actually reach that state.
// ranger.cpp/spell_pa.cpp (ambush/cast/hide/trap) remain app-compiled, so
// those four cells still dispatch through this seam like a genuine
// app-command call, as originally described.

#include "rots/core/types.h" // for the NOWHERE macro (call_special()'s in_room default)
#include "rots/persist/file_formats.h" // for the RENT_CRASH macro (crash_crashsave()'s rent_code default)

struct char_data;
struct obj_data;
struct waiting_type;
struct affected_type;

namespace rots::combat {

// The fixed ACMD signature (interpre.h's ACMD macro) every registered cell
// matches: void do_x(char_data* ch, char* argument, waiting_type* wtl,
// int cmd, int subcmd).
using acmd_fn = void (*)(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

// One enumerator per up-call target (see this header's file comment for the
// full census-C reconciliation). `count` is a sentinel bounding the backing
// array; keep it last, and keep this list alphabetical for easy diffing
// against the file comment's target list.
enum class combat_command {
    // 27th cell (Cluster B wave Task 1; cb-task-1-brief.md Step 1;
    // cb-census.md section 5.3) -- real body is act_soci.cpp's
    // ACMD(do_action), a still-app-compiled TU. script.cpp:1107's call
    // (`do_action(tmpch, curr->text, &tmpwtl, 0, 0)`) is the up-call this
    // cell inverts, the same "real body stays in its still-app owner" shape
    // as `dismount`'s own do_dismount/ranger.cpp below. Alphabetically first
    // (action < ambush).
    action,
    ambush,
    assist,
    cast,
    close,
    // 26th cell (combat-trio wave Task 1; trio-task-1-brief.md CONTROLLER
    // ADDENDUM item 3; combat-trio-census.md section 5.1) -- real body is
    // ranger.cpp's ACMD(do_dismount), a still-app-compiled combat-row TU
    // (ranger.cpp itself never needs to promote for this cell to work, the
    // same "real body stays in its still-app owner" shape as `flee`'s own
    // do_flee/act_offe.cpp). Breaks olog_hai.cpp's one direct up-call to
    // do_dismount, its only genuine combat-peer edge (census section 1).
    dismount,
    // 28th cell (Cluster B wave Task 1; cb-task-1-brief.md Step 1;
    // cb-census.md section 5.3) -- real body is act_wiz.cpp's
    // ACMD(do_emote), a still-app-compiled TU. script.cpp:964's call
    // (`do_emote(tmpch, curr->text, 0, 36, 0)`) is the up-call this cell
    // inverts, same shape as `action` above. Alphabetically between
    // `dismount` and `flee`.
    emote,
    flee,
    gen_com,
    hide,
    hit,
    lock,
    look,
    mental,
    move,
    open,
    rescue,
    rest,
    say,
    // 29th cell (Cluster B wave Task 1; cb-task-1-brief.md Step 1;
    // cb-census.md section 5.3) -- real body is act_wiz.cpp's
    // ACMD(do_shutdown), a still-app-compiled TU. shapemob.cpp:2105's call
    // (`do_shutdown(ch, mutable_arg(""), 0, 0, SCMD_SHUTDOWN)`) is the
    // up-call this cell inverts, same shape as `action`/`emote` above.
    // DESIGN-RISK NOTE (cb-census.md section 5.3): do_shutdown's real body
    // force-shuts-down the server -- its own discriminator test MUST use a
    // registered STUB (not the real ACMD) to prove dispatch, never invoking
    // the genuine shutdown body; see combat_hooks_tests.cpp's own comment at
    // its ShutdownCell suite. Alphabetically between `say` and `sit`.
    shutdown,
    sit,
    sleep,
    stand,
    stat,
    tell,
    trap,
    unlock,
    wake,
    wear,
    count
};

// Registers the real ACMD pointer for one cell. Called once per cell by
// register_combat_command_dispatch() (interpre.cpp) at boot, mirroring
// assign_spell_pointers()'s `skills[i].spell_pointer = spell_x;` pattern.
void set_combat_command(combat_command command, acmd_fn handler);

// Dispatch entry point: forwards to the registered handler for `command`.
// Unlike assign_spell_pointers()'s skills[] (where most cells are
// PERMANENTLY null by design -- physical skills have no spell function, so
// every reader null-guards before calling), every one of this table's 26
// cells is meant to be registered at boot; a null cell here means
// register_combat_command_dispatch() has not run yet, not "no handler was
// ever intended." Tripwire default: a LOGGED NO-OP, not abort -- this
// mirrors output_seam.cpp's dominant taxonomy for its six VOID forwarders
// (break_spell/abort_delay/complete_delay/send_to_all/send_to_room/
// send_to_room_except_two), not its one pointer-returning exception
// (get_from_txt_block_pool, which aborts because no safe placeholder
// txt_block* exists to return). Every ACMD here is void, so a no-op IS a
// safe placeholder: skipping a mob's command this tick is a degraded-but-
// defined outcome (the same class as entity_hooks.h's float hooks'
// 1.0f-multiplier default), never a dereference of anything.
// register_combat_command_dispatch() runs from INSIDE boot_db()
// (db_boot.cpp), in the same assign_*() sequence as assign_spell_pointers()
// -- not a run_the_game()-level pre-boot_db() registrar like the
// output_seam/entity_hooks sinks -- before clerics.cpp's/fight.cpp's real
// callers (combat-pilot wave, Task 5) ever reach issue_command(), so in
// normal operation the default does not fire -- it remains a real safety
// net for an out-of-order boot sequence or a not-yet-registered future
// caller, not a purely theoretical placeholder anymore.
void issue_command(
    combat_command command, char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

// special()'s registered-hook seam (interpre.h:99; combat-pilot wave Task 2;
// pilot-census.md section 3.1 -- NOT a 26th combat_command cell above:
// special()'s int-returning, 6-parameter shape is categorically different
// from this header's ACMD-only enum-indexed table). clerics.cpp's/
// fight.cpp's upward special(...) calls (interpre.cpp-owned) route through
// this pair -- consumer-free when built at Task 2, converted for real in
// Task 3 (clerics.cpp) and Task 5 (fight.cpp).
//
// A function-pointer TYPE cannot itself carry a default argument (defaults
// are a declaration/call-site feature in C++, not part of the type), so
// special_fn spells out interpre.h:99's full SIX parameters explicitly.
// call_special()'s own declaration below carries the `int in_room =
// NOWHERE` default instead, so a converted call site that previously wrote
// plain `special(ch, cmd, arg, callflag, wtl)` (5 args, relying on
// interpre.h:99's own default) reads identically after conversion:
// `rots::combat::call_special(ch, cmd, arg, callflag, wtl)`.
using special_fn = int (*)(
    char_data* ch, int cmd, char* arg, int callflag, waiting_type* wtl, int in_room);

// Registers the real special() pointer. Called once by
// register_combat_command_dispatch() (interpre.cpp), which already DEFINES
// special() itself -- no forward declaration needed there, unlike this
// header's ACMD table above, whose 26 cells interpre.cpp only
// forward-declares.
void set_special_handler(special_fn handler);

// Dispatch entry point, in_room defaulting to NOWHERE exactly like
// interpre.h:99. Tripwire default: a LOGGED return of 0 ("no spec-proc
// consumed the event") -- the same 0-default class pilot-census.md section
// 3.1 confirms special()'s own real callers already treat a non-1 return
// as. clerics.cpp's/fight.cpp's real special() up-calls (combat-pilot wave,
// Task 5) now route through call_special() for real, registered via
// register_combat_command_dispatch() from inside boot_db()'s assign_*()
// sequence (see issue_command()'s own tripwire comment above for the exact
// ordering) -- the same "real safety net, not a theoretical placeholder"
// posture that comment now documents.
int call_special(
    char_data* ch, int cmd, char* arg, int callflag, waiting_type* wtl, int in_room = NOWHERE);


// -----------------------------------------------------------------------
// Task 4b hooks (combat-pilot wave): four remaining fight.cpp/clerics.cpp
// seams (pilot-census.md section 3.6 for extract_char; section 7's "9
// distinct symbols" table for the limits.cpp trio; section 3.7 for the
// app-other trio). Each hook below is a single registered fn-ptr, backed in
// combat_hooks.cpp exactly like g_special_handler above -- NOT a
// combat_command enum cell, since none of these seven share the fixed ACMD
// signature the 26-cell table dispatches. CONSUMER-FREE at Task 4b landing
// time: fight.cpp/clerics.cpp still called the real global functions
// directly then (still app-compiled -- ROTS_SERVER_SOURCES -- a legal
// same-tier call at that point). STATUS UPDATE (combat-pilot wave):
// fight.cpp's/clerics.cpp's real call sites now route through these
// rots::combat:: dispatch entry points for real -- clerics.cpp's own
// conversions landed in Task 3 (while still app-compiled), fight.cpp's in
// Task 5(a) (also while still app-compiled); both files then joined
// rots_combat together in Task 5(b)'s joint membership move (a mutual
// intra-subset dependency made a standalone promotion of either
// impossible -- see the migration playbook's "intra-subset rule").
// -----------------------------------------------------------------------

// extract_char() RE-HOMED to entity_hooks.h (l4-seed wave, Task 1;
// l4-task-1-brief.md Step 2a; l4-census.md section 3.4): both rots_world
// and rots_combat need this inversion (rots_world already PUBLIC-links
// RotS::entity), so the setter/dispatch/typedef moved to the shared L2
// header rather than staying combat-only. See entity_hooks.h's own
// extract_char_fn comment for the full history; fight.cpp's three call
// sites now read rots::entity::extract_char(...).

// gain_exp()/gain_exp_regardless() (limits.h:33-34, limits.cpp:413/437;
// pilot-census.md section 7.4/7.5) -- gain_exp()'s own body only calls
// gain_exp_regardless() (same file), and gain_exp_regardless() calls
// advance_mini_level() -> advance_level()/advance_level_prof(), limits.cpp's
// own multi-function leveling subsystem -- not a single-symbol relocation
// candidate, hence HOOK rather than MOVE for both. Two separate fn-ptrs
// (not one struct), matching this header's existing one-fn-ptr-per-symbol
// convention (e.g. set_special_handler above). limits.cpp registers both
// real bodies via register_gain_exp_hook()/register_gain_exp_regardless_hook()
// (limits.h/limits.cpp) -- an app-side registrar pair when this hook was
// built (limits.cpp was still app-compiled at the time); see the
// POST-MEMBERSHIP STATUS UPDATE paragraph below the declarations for
// where limits.cpp actually landed. Tripwire default: a LOGGED no-op
// (void class, same taxonomy as extract_char above).
using gain_exp_fn = void (*)(char_data* ch, int gain);
void set_gain_exp_hook(gain_exp_fn hook);
void gain_exp(char_data* ch, int gain);

using gain_exp_regardless_fn = void (*)(char_data* ch, int gain);
void set_gain_exp_regardless_hook(gain_exp_regardless_fn hook);
void gain_exp_regardless(char_data* ch, int gain);

// remove_fame_war_bonuses() (limits.h:40, limits.cpp:1214; pilot-census.md
// section 7.6) -- pulls an 8-function/~190-line same-file
// assign_pk_bonuses()/set_player_*() cluster, out of this task's
// single-symbol relocation scope, hence HOOK. limits.cpp registers the real
// body via register_remove_fame_war_bonuses_hook() (limits.h/limits.cpp),
// riding the same registrar file as the gain_exp pair above. Tripwire
// default: LOGGED no-op (void class).
using remove_fame_war_bonuses_fn = void (*)(char_data* ch, affected_type* pkaff);
void set_remove_fame_war_bonuses_hook(remove_fame_war_bonuses_fn hook);
void remove_fame_war_bonuses(char_data* ch, affected_type* pkaff);

// POST-MEMBERSHIP STATUS UPDATE (behavior wave Task 3, Step 5's
// cleanup/verification pass; .superpowers/sdd/bw-task-3-brief.md;
// bw-task-3-report.md): limits.cpp joined ROTS_COMBAT_SOURCES in this
// task's Step 1, so the register_gain_exp_hook()/
// register_gain_exp_regardless_hook()/register_remove_fame_war_bonuses_
// hook() registrar calls above and the real bodies they point at now
// resolve INTRA-LIB (both live in rots_combat). This is NOT mere
// self-registration eligible for removal, though: fight.cpp (SIX call
// sites: fight.cpp:936/1084/1086/1116/1330/1974) and clerics.cpp (one
// call site) -- both already RotS::combat
// members since the combat-pilot wave -- still dispatch through
// rots::combat::gain_exp()/gain_exp_regardless()/remove_fame_war_bonuses()
// rather than calling limits.cpp's globals directly (verified by grep,
// not assumed -- see bw-task-3-report.md for the exact call sites). All
// three hooks and their registrars therefore STAY: deleting them would
// break real, currently-live dispatch. Converting fight.cpp's/
// clerics.cpp's call sites back to direct limits.cpp calls (now legal,
// since both sides are intra-lib) remains a deferred follow-on call-site
// simplification, out of this cleanup task's scope.

// App-other trio (pilot-census.md section 3.7) -- Crash_crashsave
// (objsave.cpp), call_trigger (script.cpp), pkill_create (pkill.cpp). Each
// owning TU stays app-compiled after this wave and registers its own real
// body via its own per-owner registrar (Crash_crashsave:
// register_crash_crashsave_hook(), handler.h/objsave.cpp; call_trigger:
// register_call_trigger_hook(), script.h/script.cpp; pkill_create:
// register_pkill_create_hook(), pkill.h/pkill.cpp).

// Crash_crashsave() (handler.h:254, objsave.cpp:938) -- void return, no
// tripwire-semantics concern (pilot-census.md section 3.7). Tripwire
// default: LOGGED no-op (void class).
using crash_crashsave_fn = void (*)(char_data* ch, int rent_code);
void set_crash_crashsave_hook(crash_crashsave_fn hook);
void crash_crashsave(char_data* ch, int rent_code = RENT_CRASH);

// call_trigger() (script.h:165, script.cpp:666) -- int return. fight.cpp:1003
// (`if (call_trigger(ON_DIE, dead_man, killer, 0) == FALSE) { ... return
// without dying ... }`) and fight.cpp:1843
// (`if (!call_trigger(ON_DAMAGE, victim, attacker, 0))`) both treat FALSE as
// "a script vetoed this event" -- an unregistered hook must NOT silently
// immortalize/veto-by-default, so the tripwire default is a LOGGED return of
// TRUE ("no script attached / proceed normally"), the one VALUE-returning
// hook in this task whose safe default is a documented non-zero constant
// rather than the void class's plain no-op. This taxonomy choice is
// load-bearing correctness, not mere convention -- see this symbol's
// dedicated discriminator test (combat_hooks_tests.cpp,
// CombatHooksCallTrigger.DispatchDefaultsToLoggedTrueWhenUnregistered) for
// the MANDATORY default-path proof pilot-task-4b-brief.md requires.
using call_trigger_fn = int (*)(int trigger_type, void* subject, void* subject2, void* subject3);
void set_call_trigger_hook(call_trigger_fn hook);
int call_trigger(int trigger_type, void* subject, void* subject2, void* subject3);

// pkill_create() (pkill.h:27, pkill.cpp:598) -- void return, no
// tripwire-semantics concern. Tripwire default: LOGGED no-op (void class).
using pkill_create_fn = void (*)(char_data* victim);
void set_pkill_create_hook(pkill_create_fn hook);
void pkill_create(char_data* victim);


// -----------------------------------------------------------------------
// Behavior-wave Task 1 hooks (mobact.cpp -> rots_script / limits.cpp ->
// rots_combat, .superpowers/sdd/bw-task-1-brief.md; census
// .superpowers/sdd/bw-census.md). CONSUMER-FREE at landing time (Task 1):
// neither mobact.cpp nor limits.cpp had joined a library yet, and no
// existing call site had converted -- both TUs stayed app-compiled
// (ROTS_SERVER_SOURCES). Task 2 converted the call site
// (limits.cpp:1406 -> dispatch_one_mobile_activity()) and Task 3 landed
// both memberships (bw-task-3-brief.md): mobact.cpp -> ROTS_SCRIPT_SOURCES
// (Step 2, L4), limits.cpp -> ROTS_COMBAT_SOURCES (Step 1, L3). This hook
// is now a LIVE, PERMANENT cross-tier inversion, not a placeholder: it
// stays exactly as-is regardless of both TUs being lib members, because
// dispatch flows L3 (limits.cpp, the caller) -> L4 (mobact.cpp, the real
// body) -- the opposite of the normal L4->L3 downward link direction, so a
// direct call would be just as illegal as before. mobact.cpp's registrar
// call is now a legal L4->L3 downward registration into this library's
// setter (see the ROTS_SCRIPT_SOURCES comment in CMakeLists.txt for the
// full post-membership picture).
// -----------------------------------------------------------------------

// one_mobile_activity() (mobact.cpp:71; census section 3) -- limits.cpp's
// SOLE external call site (limits.cpp:1398, inside the SPELL_ACTIVITY case
// of the affect-processing switch: `one_mobile_activity(i);`) is the
// one-directional cross-edge this hook inverts: mobact.cpp's own :61
// self-call inside mobile_activity()'s loop stays a direct intra-file call
// regardless of tier (census section 3 confirms zero mobact.cpp reference
// to any limits-owned symbol, so this is a genuinely one-directional gate).
// mobact.cpp registers the real body via its own register_one_mobile_
// activity_hook() registrar (mobact.cpp; declared locally at each call
// site, mirroring comm.cpp's own signal_setup()-shaped local forward
// declaration -- mobact.cpp has no dedicated header), called once from
// run_the_game()/gtest_main.cpp's main(), before boot_db() -- same
// convention as every other *_hooks.h registrar. Tripwire default: a LOUD
// LOGGED TRIPWIRE + no-op (void class; matches script_hooks.h's
// command_interpreter_fn taxonomy -- there is no return value to safely
// omit or dangerously fabricate, but an unregistered per-tick mob-activity
// dispatch is still a real error worth logging loudly).
using mobile_activity_fn = void (*)(char_data* ch);
void set_one_mobile_activity_hook(mobile_activity_fn hook);
void dispatch_one_mobile_activity(char_data* ch);

// Crash_idlesave()/Crash_extract_objs() (objsave.cpp:980/:898; census
// section 8) -- limits.cpp's check_idling() calls both (Crash_idlesave at
// limits.cpp:596; Crash_extract_objs at limits.cpp:602/:606), mirroring the
// existing Crash_crashsave hook exactly (same App-other-trio shape, same
// owning TU, same registrar file). objsave.cpp registers both real bodies
// via register_crash_idlesave_hook()/register_crash_extract_objs_hook()
// (handler.h/objsave.cpp), riding the same registrar file as
// register_crash_crashsave_hook() above. Tripwire default: LOGGED no-op
// (void class, same taxonomy as crash_crashsave_fn above).
using crash_idlesave_fn = void (*)(char_data* ch);
void set_crash_idlesave_hook(crash_idlesave_fn hook);
void crash_idlesave(char_data* ch);

using crash_extract_objs_fn = void (*)(obj_data* obj);
void set_crash_extract_objs_hook(crash_extract_objs_fn hook);
void crash_extract_objs(obj_data* obj);

} // namespace rots::combat
