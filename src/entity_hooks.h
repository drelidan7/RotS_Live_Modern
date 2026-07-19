#pragma once
// entity_hooks.h -- dependency-inversion seam for entity_lifecycle.cpp's two
// remaining upward edges (spec Sec13 pattern, mirroring output_seam.h). See
// each hook's comment below for the call site it replaces and why a null
// default is safe until registration runs. Backing storage + dispatch
// helpers are defined in entity_lifecycle.cpp itself (not a separate .cpp,
// unlike output_seam.cpp) -- see that file's "entity_hooks.h dispatch"
// section.

struct char_data;
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

}
