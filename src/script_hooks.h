#pragma once
// script_hooks.h -- dependency-inversion seam for rots_script's
// (mudlle.cpp/mudlle2.cpp, joined by script_hooks.cpp itself -- l4-seed
// wave Task 3) two upward edges into app-tier TUs (spec Sec13 pattern,
// mirroring world_hooks.h/persist_hooks.h). See each hook's comment below
// for the call site it replaces and why its default is a deliberately loud
// logged tripwire (command_interpreter) or a documented abort tripwire
// (PERS) -- l4-task-1-brief.md's CONTROLLER ADDENDUM items 5 and
// l4-census.md sections 3.1/3.2 adjudicate both shapes.
//
// Backing storage + dispatch for BOTH hooks live in script_hooks.cpp (see
// that file's own banner comment for the full history): command_interpreter
// moved there in Task 3 from its original T1 placement in interpre.cpp --
// that file already DEFINES command_interpreter() itself and stays app-tier
// permanently, so once mudlle.cpp's call site converted to
// rots::script::dispatch_command_interpreter() in Task 2, the T1 placement
// became a genuine rots_script -> app upward edge the moment mudlle.cpp
// joined rots_script; caught by rots_script_linkcheck's first ordinary
// link (two undefined symbols: dispatch_pers/dispatch_command_interpreter),
// not silently missed -- a separate negative-probe exercise (CMakeLists.txt)
// only confirmed the checker is non-vacuous. PERS's backing storage has
// lived in
// script_hooks.cpp since T1 (PERS's real body and registrar are
// utility.cpp, a different owner, so it never had a single natural owning
// TU -- the same "seam header, no single owning caller" shape as
// combat_hooks.cpp/combat_hooks.h one tier down).

struct char_data;
struct waiting_type;

namespace rots::script {

// mudlle.cpp:862's one live command_interpreter() call site (inside the
// SPECIAL_LIST 'C' case; l4-census.md section 3.1 -- every other
// command_interpreter(...) call in that block is commented-out dead text).
// interpre.cpp registers the real body (command_interpreter() is DEFINED
// there, so registration needs no forward declaration) in the existing
// pre-boot_db() boot sequence (run_the_game(), comm.cpp), mirroring this
// header's sibling hooks. Void-returning -- census section 3.1 resolves the
// brief's "int-returning?" open question to NO -- so the null default is a
// LOUD LOGGED TRIPWIRE + no-op (world_hooks.h's boot_shops_fn class), not
// the abort-tripwire class PERS below uses: there is no return value to
// safely omit or dangerously fabricate, but an unregistered mob-program
// command dispatch is still a real error worth logging loudly.
using command_interpreter_fn = void (*)(char_data* ch, char* argument_chr,
    waiting_type* argument_wtl);
void set_command_interpreter_hook(command_interpreter_fn hook);
void dispatch_command_interpreter(char_data* ch, char* argument_chr, waiting_type* argument_wtl);

// mudlle2.cpp:286's one live PERS() call site (`strcpy(targ,
// PERS(SPECIAL_LIST(host).ptr.ch, host, FALSE, FALSE));`; l4-census.md
// section 3.2 -- confirmed the only PERS( call in either mudlle.cpp or
// mudlle2.cpp). PERS's own body (utility.cpp) cannot itself relocate -- its
// CC_USE/CC_NORM color-sequence dependency is genuinely app-tier (color.cpp,
// session-tier per the world-growth census's own tier legend) -- so this
// hook inverts only the CALL, the same "hook, not relocate" shape as this
// wave's equip_char/pkill-fame overturns. utility.cpp registers the real
// PERS body in the existing pre-boot_db() boot sequence (run_the_game(),
// comm.cpp). Pointer-returning into a function-local `static char
// name[128]` buffer that the caller immediately dereferences -- the
// abort-tripwire class (entity_hooks.h's get_txt_block_fn pair,
// output_seam.h's get_from_txt_block_pool(string_view) forwarder): there is
// no safe placeholder char* to return, so an unregistered hit is a loud
// tripwire log FOLLOWED BY ABORT rather than a safe fallback value.
// Untested-by-design for its unregistered path, same as every other abort
// tripwire in the tree (no death test); the registered path is this hook's
// discriminator coverage.
using pers_fn = char* (*)(char_data * target, char_data* observer, int capitalize,
    int force_visible);
void set_pers_hook(pers_fn hook);
char* dispatch_pers(char_data* target, char_data* observer, int capitalize, int force_visible);

// virt_program_number() (comm.h:144, spec_ass.cpp:315; behavior wave Task 1;
// CONTROLLER ADDENDUM item 3; census section 4 -- rider-gate count 1 of the
// pre-authorized <=3, no auto-STOP). mobact.cpp's two call sites
// (mobact.cpp:64/:126, consumer-free this task) are the sole edge this cell
// exists for: a full mobact.cpp call-shape sweep against spec_ass.cpp/
// spec_pro.cpp found zero further same-shape (void*-returning
// spec-proc-dispatcher) or different-shape edge. spec_ass.cpp registers the
// real body via register_virt_program_number_hook() (comm.h/spec_ass.cpp --
// comm.h already declares virt_program_number() itself, so it also hosts
// this registrar's declaration), called once from run_the_game()/
// gtest_main.cpp's main(), before boot_db(). Abort-tripwire default
// (pointer return): there is no safe placeholder SPECIAL(*) to return --
// mirrors this header's own pers_fn taxonomy above, not
// command_interpreter_fn's loud-logged-no-op class.
using virt_program_fn = void* (*)(int number);
void set_virt_program_number_hook(virt_program_fn hook);
void* dispatch_virt_program_number(int number);

// virt_assignmob() (interpre.h:119, spec_ass.cpp:491; Cluster B wave Task 1;
// cb-task-1-brief.md Step 2; cb-census.md section 5.2 -- rider gate edge 2
// of the pre-authorized <=3, no auto-STOP). shapemob.cpp:1838's one call
// site (`virt_assignmob(mob_proto + number);`) is the sole edge this cell
// exists for: a full sweep of all 7 Cluster B TUs (script.cpp +
// shape{mdl,mob,obj,rom,script,zon}.cpp) for `virt_*`/`assign_*`/`special(`
// found zero further same-shape (void-returning, single char_data*
// spec-proc-assignment) or different-shape edge. spec_ass.cpp registers the
// real body via register_virt_assignmob_hook() (spec_ass.cpp -- the
// virt_program_number_hook precedent above), called once from
// run_the_game()/gtest_main.cpp's main(), before boot_db(). Abort-tripwire
// default (this cell has no return value to safely omit, but its real body
// mutates mob_index[mob->nr].func -- a caller relying on that mutation
// having happened must not silently proceed as if it had): mirrors this
// header's pers_fn/virt_program_fn taxonomy above, not
// command_interpreter_fn's loud-logged-no-op class.
using virt_assignmob_fn = void (*)(char_data* mob);
void set_virt_assignmob_hook(virt_assignmob_fn hook);
void dispatch_virt_assignmob(char_data* mob);

} // namespace rots::script
