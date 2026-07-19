#pragma once
// entity_hooks.h -- dependency-inversion seam for entity_lifecycle.cpp's
// upward edges (spec Sec13 pattern, mirroring output_seam.h), plus (as of
// placement-seam Task 1) placement.cpp's three world-resolver hooks. See
// each hook's comment below for the call site it replaces and why a null
// default is safe until registration runs. Backing storage + dispatch
// helpers for entity_lifecycle.cpp's own hooks are defined in
// entity_lifecycle.cpp itself (not a separate .cpp, unlike output_seam.cpp)
// -- see that file's "entity_hooks.h dispatch" section; the three resolver
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

// Resolver seam for placement.cpp's three id->pointer lookups (placement-seam
// Task 1, spec "location representation" section). world[]/zone_table/
// obj_index (their storage AND their top_of_world/top_of_zone_table/
// top_of_objt bounds counters) live in rots_world (L3): db_world.cpp/
// zone_load.cpp register the real implementations via
// register_world_resolver_hooks() (db.h; defined in db_world.cpp) in
// run_the_game(), before boot_db() -- same convention as this header's
// hooks above.
//
// CONTRACT (controller-adjudicated, placement-seam Task 1 -- see
// task-1-report.md): each REGISTERED resolver is bounds-checked by its own
// implementation and returns nullptr for an out-of-range id (rnum >=
// top_of_world / znum >= top_of_zone_table / item_number >= top_of_objt),
// a resolved pointer otherwise. A null return from a registered hook is a
// normal "absent" result (Stage-2-aligned: "no location = absent"), NOT a
// failure -- callers that historically bounds-checked before indexing
// (e.g. recount_light_room's original `if (room < 0 || room >=
// top_of_world) return;`) now test the returned pointer instead, with
// byte-identical in-range behavior and a deterministic (rather than
// undefined) result out of range; callers that historically indexed
// unchecked (e.g. get_char_room's `world[room].people`) now dereference
// the resolved pointer unchecked too -- their in-range behavior is
// identical, and out-of-range goes from UB to a deterministic null-deref.
// This is DISTINCT from the tripwire-abort default dispatched in
// placement.cpp, which only fires when NO hook has been registered at all
// (a real, unreachable-in-ageland failure) -- there is no safe placeholder
// pointer for "resolver never registered," the same rationale as this
// header's txt-block-pool pair above.
using room_resolver_fn = room_data* (*)(int rnum);
using zone_resolver_fn = zone_data* (*)(int znum);
using obj_index_resolver_fn = index_data* (*)(int item_number);
void set_room_resolver_hook(room_resolver_fn hook);
void set_zone_resolver_hook(zone_resolver_fn hook);
void set_obj_index_resolver_hook(obj_index_resolver_fn hook);

}
