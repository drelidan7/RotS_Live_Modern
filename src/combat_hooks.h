#pragma once
// combat_hooks.h -- boot-registered command-dispatch seam for the four
// DEFER-11 combat-row TUs (mobact.cpp/spec_pro.cpp/ranger.cpp/fight.cpp)
// that issue player/mob commands upward into the app-command tier
// (blocker-buster wave Task 2; plan
// docs/superpowers/plans/2026-07-19-blocker-buster.md; census
// .superpowers/sdd/blocker-census.md section C). Modeled on
// assign_spell_pointers() (spell_pa.cpp/spells.h, entity-seed Task 1): an
// enum-indexed array of ACMD function pointers, null-initialized at static
// init and populated once at boot by register_combat_command_dispatch()
// (interpre.cpp -- see that file for why it, not a TU of this header's own,
// does the populating: interpre.cpp already forward-declares every ACMD
// target below for its own command-interpreter table, the same reason
// spell_pa.cpp -- already visiting every spell_*() body it wires -- was
// assign_spell_pointers()'s home).
//
// NO CALL-SITE CONVERSION THIS WAVE: mobact/spec_pro/ranger/fight keep
// calling do_hit()/do_flee()/etc. directly (they are still app-compiled --
// ROTS_SERVER_SOURCES -- so that is a legal same-tier call today). This seam
// exists so that when one of those four TUs joins rots_combat in a future
// wave, EACH of its up-calls converts to rots::combat::issue_command(...)
// instead, resolving downward through this table rather than welding back
// up into an app-tier symbol the library cannot see.
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
// ACMD in clerics.cpp (a DEFER-11 TU), the same cross-row class as
// ambush/cast/hide/trap below. It passed both exclusion tests above (a real
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
// ranger.cpp/spell_pa.cpp/clerics.cpp -- other DEFER-11 combat-row TUs, not
// app-command tier -- rather than an act_*.cpp file. They are included
// anyway: DEFER-11 promotion order is not fixed, so a cross-TU call between
// two not-yet-promoted combat-row TUs must route through this seam exactly
// like a genuine app-command call until (and unless) both sides of the call
// promote together, at which point the registered pointer simply becomes an
// intra-lib address instead -- see task-2-report.md for the per-cell
// breakdown.

struct char_data;
struct waiting_type;

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
    ambush,
    assist,
    cast,
    close,
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
// every reader null-guards before calling), every one of this table's 25
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
// 1.0f-multiplier default), never a dereference of anything. This wave
// never exercises the default in ageland (nothing calls issue_command() yet
// -- no call-site conversion), so the choice is precautionary, not load-
// bearing this wave -- documented for whichever future wave's first real
// caller relies on it.
void issue_command(
    combat_command command, char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

} // namespace rots::combat
