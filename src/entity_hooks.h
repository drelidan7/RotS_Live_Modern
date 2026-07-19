#pragma once
// entity_hooks.h -- dependency-inversion seam for entity_lifecycle.cpp's
// upward edges (spec Sec13 pattern, mirroring output_seam.h), plus (as of
// placement-seam Task 1) placement.cpp's four world-resolver hooks (room,
// its room_by_id/room_by_id_total variant pair, zone, and obj-index). See
// each hook's comment below for the call site it replaces and why a null
// default is safe until registration runs. Backing storage + dispatch
// helpers for entity_lifecycle.cpp's own hooks are defined in
// entity_lifecycle.cpp itself (not a separate .cpp, unlike output_seam.cpp)
// -- see that file's "entity_hooks.h dispatch" section; the four resolver
// hooks' backing storage + dispatch live in placement.cpp instead, next to
// their sole caller-file's Stage-1 API functions.

struct char_data;
struct room_data;
struct zone_data;
struct index_data;
struct txt_block;

namespace rots::entity {

// Teardown notification: free_char() (entity_lifecycle.cpp) fires this for
// every character destroyed. objsave.cpp registers
// clear_account_backed_object_bytes_for_character at boot; null default is a
// provable no-op (the staged-object map can only gain entries via the login
// flow, which never runs before registration -- see the former ledger entry).
using char_teardown_fn = void (*)(const char_data* character);
void set_char_teardown_hook(char_teardown_fn hook);

// recalc_abilities()' weapon branch (entity_lifecycle.cpp:619-620) queries the
// weapon-master attack-speed multiplier. wild_fighting_handler.cpp registers
// the real player_spec::weapon_master_handler-backed impl in run_the_game()
// BEFORE boot_db(), so ageland has no unregistered window; the null default
// (tripwire log + 1.0f) reproduces the converter's stub exactly (equipment is
// always null there, so the branch is unreachable anyway).
using attack_speed_fn = float (*)(char_data* character);
void set_attack_speed_multiplier_hook(attack_speed_fn hook);

// get_energy_regen()'s wild-fighting attack-speed query (char_utils.cpp).
// wild_fighting_handler.cpp registers the real construct-and-query pre-boot;
// null default = tripwire log + 1.0f (converter: unreachable-by-invariant,
// same class as the weapon-master hook).
using wild_attack_speed_fn = float (*)(const char_data* character);
void set_wild_attack_speed_multiplier_hook(wild_attack_speed_fn hook);

// on_attacked_character()'s PK notification (char_utils_combat.cpp).
// big_brother.cpp registers a forwarder to big_brother::instance().
// on_character_attacked_player() pre-boot; null default = tripwire no-op
// (combat never runs before run_the_game's registrations in ageland).
using attacked_player_fn = void (*)(const char_data* attacker, const char_data* attacked);
void set_attacked_player_hook(attacked_player_fn hook);

// target_data::cleanup()/operator=()'s txt-block pool traffic
// (entity_lifecycle.cpp, relocated verbatim from interpre.cpp -- world-seed
// Task 2). comm.cpp owns the actual pool (get_from_txt_block_pool/
// put_to_txt_block_pool) because it is entangled with comm.cpp's
// descriptor/output-buffer machinery (large_outbuf/bufpool), not a leaf
// module -- so this hook pair inverts entity_lifecycle.cpp's edge onto it
// instead of relocating the pool itself. comm.cpp registers the real pool
// functions in run_the_game() before boot_db(); the null default is a loud
// tripwire log FOLLOWED BY ABORT, not a safe fallback value -- unlike this
// header's float-returning hooks above, there is no placeholder txt_block*
// that would be safe to return: a TARGET_TEXT copy immediately dereferences
// the pointer (ptr.text->text), so a silently-returned null would surface as
// a confusing null-deref far from the real cause instead of failing loudly
// at the hook boundary.
using get_txt_block_fn = struct txt_block* (*)();
using put_txt_block_fn = void (*)(struct txt_block*);
void set_get_txt_block_pool_hook(get_txt_block_fn hook);
void set_put_txt_block_pool_hook(put_txt_block_fn hook);

// Dispatch entry points for the two hooks above. Unlike this header's
// existing two hooks -- each dispatched only from entity_lifecycle.cpp, the
// same TU that owns their backing storage -- these are dispatched from
// char_utils.cpp / char_utils_combat.cpp respectively, so (unlike
// entity_lifecycle.cpp's file-local dispatch_char_teardown()/
// dispatch_attack_speed_multiplier()) they need an external-linkage
// declaration here for those other TUs to call. Defined in
// entity_lifecycle.cpp, next to their backing storage.
float dispatch_wild_attack_speed_multiplier(const char_data* character);
void dispatch_attacked_player(const char_data* attacker, const char_data* attacked);

// Resolver seam for placement.cpp's world-id->pointer lookups (placement-seam
// Task 1, spec "location representation" section; room resolver split into
// two variants at Task 1's post-review follow-up -- see below). world[]/
// zone_table/obj_index (their storage AND their top_of_world/
// top_of_zone_table/top_of_objt bounds counters) live in rots_world (L3):
// db_world.cpp/zone_load.cpp register the real implementations via
// register_world_resolver_hooks() (db.h; defined in db_world.cpp) in
// run_the_game(), before boot_db() -- same convention as this header's
// hooks above.
//
// TWO ROOM-RESOLVER VARIANTS (reviewer-corrected, placement-seam Task 1
// follow-up -- see task-1-report.md): room_data::operator[] (db_world.cpp,
// world[]'s actual indexing operator) is a graceful TOTAL function, not
// partial -- out-of-range input logs and returns a fallback room
// (world[0] for negative, world[r_immort_start_room] for too-large, or
// exit(0) in the one case where even that fallback is itself
// out-of-range) rather than invoking undefined behavior. So a caller that
// historically indexed `world[x]` unchecked was never in UB territory --
// it got a defined (if degraded) room back. That history means ONE
// nullptr-on-invalid resolver would silently narrow behavior for such
// callers, so there are two:
//
//   - room_by_id(rnum): nullptr for an out-of-range id (rnum < 0 or rnum
//     >= top_of_world), a resolved pointer otherwise. For callers whose
//     ORIGINAL code bounds-checked BEFORE indexing (they need the
//     validity signal to reproduce their own early-return/no-op, not
//     operator[]'s fallback, which they never reached). Note the
//     top_of_world-EXCLUSIVE bound also excludes the one valid "dummy"
//     room actually allocated at index top_of_world itself (see
//     db_world.cpp's load_rooms()/create_room() comments) -- faithful to
//     those bounds-checking callers, which historically excluded that
//     index too, but a caveat for any FUTURE caller that means to reach
//     it deliberately (none does, this task).
//   - room_by_id_total(rnum): TOTAL -- delegates straight to
//     room_data::operator[], preserving its exact fallback room AND
//     mudlog side effects for every input, in range or not. For callers
//     whose original code indexed `world[x]` UNCHECKED -- restores their
//     historical graceful-degradation behavior exactly, rather than
//     replacing it with a new (and narrower) null-then-crash contract.
//
// zone_by_id/obj_index_by_id have only ONE (nullptr-on-invalid) variant
// each, deliberately asymmetric with the room resolver: zone_table/
// obj_index are raw C arrays with no operator[]-style fallback wrapper,
// so their historical unchecked-index behavior for an out-of-range id was
// genuinely undefined (real UB, not a graceful degrade) -- a nullptr
// contract is a strict improvement there, not a behavior change requiring
// a total counterpart. (No Task 1 caller exercises either boundary yet;
// see each impl's own comment for the re-verification caveat left for
// the tasks that first call them.)
//
// Both room-resolver variants, like zone_by_id/obj_index_by_id, still
// share the tripwire-abort-on-UNREGISTERED-hook default dispatched in
// placement.cpp -- a hook returning nullptr (room_by_id) or a fallback
// room (room_by_id_total) is a normal REGISTERED result, distinct from no
// hook being registered at all (a real, unreachable-in-ageland failure);
// there is no safe placeholder pointer for that case, the same rationale
// as this header's txt-block-pool pair above.
using room_resolver_fn = room_data* (*)(int rnum);
using room_total_resolver_fn = room_data* (*)(int rnum);
using zone_resolver_fn = zone_data* (*)(int znum);
using obj_index_resolver_fn = index_data* (*)(int item_number);
void set_room_resolver_hook(room_resolver_fn hook);
void set_room_total_resolver_hook(room_total_resolver_fn hook);
void set_zone_resolver_hook(zone_resolver_fn hook);
void set_obj_index_resolver_hook(obj_index_resolver_fn hook);

}
