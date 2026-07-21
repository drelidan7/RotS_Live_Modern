#pragma once
// script_hooks.h -- dependency-inversion seam for rots_script's (the future
// mudlle.cpp/mudlle2.cpp library, l4-seed spec) two upward edges into
// app-tier TUs (spec Sec13 pattern, mirroring world_hooks.h/persist_hooks.h).
// See each hook's comment below for the call site it replaces and why its
// default is a deliberately loud logged tripwire (command_interpreter) or a
// documented abort tripwire (PERS) -- l4-task-1-brief.md's CONTROLLER
// ADDENDUM items 5 and l4-census.md sections 3.1/3.2 adjudicate both shapes.
// CONSUMER-FREE at this task's landing: mudlle.cpp's/mudlle2.cpp's own call
// sites do not convert yet (they still call command_interpreter()/PERS()
// directly, a legal same-tier app call today) -- see this task's report for
// the promotion order (rots_pathfind before rots_script, per the census's
// Membership plan) that will make the conversion necessary.
//
// Backing storage + dispatch for command_interpreter live in interpre.cpp
// (l4-task-1-brief.md Step 1): that file already DEFINES command_interpreter
// itself, the same "the TU that already visits the target hosts the
// registrar" precedent as assign_spell_pointers()/
// register_combat_command_dispatch() (interpre.cpp). Backing storage +
// dispatch for PERS live in script_hooks.cpp instead: PERS's real body and
// registrar are utility.cpp, a different owner than interpre.cpp, and
// neither mudlle.cpp nor mudlle2.cpp (PERS's eventual caller) is being
// converted this task -- so, like combat_hooks.cpp (a seam header with
// several FUTURE callers rather than one owning TU), PERS gets its own small
// backing-storage TU rather than folding into interpre.cpp's file.

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

} // namespace rots::script
