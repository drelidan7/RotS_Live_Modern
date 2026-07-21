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
struct obj_data;
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

// extract_char() (handler.h:197, handler.cpp:498/503; pilot-census.md
// section 3.6; RE-HOMED from combat_hooks.h to this L2 header, l4-seed
// wave Task 1, l4-task-1-brief.md Step 2a, l4-census.md section 3.4):
// originally landed in rots_combat's own combat_hooks.h (combat-pilot wave
// Task 4b) since fight.cpp was its only converted caller at the time. Both
// rots_world (zone.cpp, once promoted) and rots_combat (fight.cpp) need
// this same inversion, and both libraries already PUBLIC-link RotS::entity
// (CMakeLists.txt), so the setter/dispatch/typedef move here rather than
// staying combat-only -- a single L2 inversion genuinely shared by both L3
// bands, matching this header's own file-comment charter. handler.cpp
// defines two overloads that are really one body: the 1-arg form forwards
// unconditionally to the 2-arg form with `new_room = -1` as a sentinel
// (matching handler.h:197's own `int new_room = -1` default), so a SINGLE
// registered fn-ptr carrying the 2-arg shape covers both call arities --
// the dispatch overloads below reproduce that same forward exactly.
// handler.cpp registers the real 2-arg body via register_extract_char_hook()
// (handler.h/handler.cpp), an app-side registrar (handler.cpp stays
// app-compiled) -- unchanged by this re-home, only its target namespace
// changes. Tripwire default: a LOGGED no-op (void class, same taxonomy as
// this header's set_attacked_player_hook()/set_poison_removal_hook() above).
using extract_char_fn = void (*)(char_data* ch, int new_room);
void set_extract_char_hook(extract_char_fn hook);
void extract_char(char_data* ch, int new_room);
void extract_char(char_data* ch);

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

// Poison-removal notification (blocker-buster wave Task 3, census section E):
// containment.cpp's obj_from_char() fires this from its equipment-fallback
// branch, immediately after calling detach_equipment() (equipment.cpp, L2
// primitive) to strip a genuinely EQUIPPED item, whenever doing so has just
// cured the wearer's poison affect -- obj_from_char() captures `was_poisoned
// = IS_AFFECTED(character, AFF_POISON)` before the detach_equipment() call
// and fires this hook only if that was true and IS_AFFECTED(character,
// AFF_POISON) is false afterward, the exact guard handler.cpp's app-tier
// unequip_char() wrapper still evaluates for its own direct (non-scripted)
// callers. The registered implementation reproduces that wrapper's poison
// damage()/raw_kill() block EXACTLY: the block's `dam`/`attacktype`/
// `hit_location` arguments are fixed literals, not per-call state, so a bare
// char_data* is enough to replay it verbatim. Firing a notification instead
// of calling unequip_char() directly is what lets obj_from_char() move to
// containment.cpp (L2) at all -- calling the app-tier wrapper from L2 would
// recreate the exact EntityLayerAcyclicity edge placement-seam Task 2's own
// linkcheck already rejected once. See containment.cpp's obj_from_char()
// relocation comment and .superpowers/sdd/task-3-report.md for the live
// mudscript counter-example (script.cpp's SCRIPT_ASSIGN_EQ +
// SCRIPT_OBJ_FROM_CHAR) this hook exists to preserve.
//
// fight.cpp registers the real damage()/raw_kill() implementation in
// run_the_game() before boot_db() -- damage() is declared in handler.h;
// raw_kill() has no header declaration anywhere in the tree (every existing
// caller, including the registered implementation itself, forward-declares
// it locally). Unregistered-fire is a tripwire log with NO abort -- unlike
// this header's txt-block-pool pair above, there is nothing to dereference
// on a void-returning hook, so a silent skip cannot surface as a confusing
// null-deref; this joins set_attacked_player_hook()'s class instead
// (tripwire log, safe no-op) because registration in run_the_game()'s /
// gtest_main.cpp's main() always precedes any path that could reach it:
// obj_from_char()'s equipment-fallback branch never runs before boot in
// ageland, and rots_convert links containment.cpp into rots_entity but its
// own call graph (convert_main.cpp's player-file conversion) never calls
// obj_from_char() at all.
using poison_removal_fn = void (*)(char_data* character);
void set_poison_removal_hook(poison_removal_fn hook);

// big_brother's PK-target-validity gate (combat-pilot wave Task 2, brief
// .superpowers/sdd/pilot-task-2-brief.md; census
// .superpowers/sdd/pilot-census.md section 3.13): clerics.cpp's/fight.cpp's
// upward game_rules::big_brother::instance().is_target_valid() calls.
// big_brother.h declares TWO overloads with genuinely different bodies, not
// one function with a default argument -- clerics.cpp calls the 2-arg form,
// fight.cpp the 3-arg (skill_id) form, and the 3-arg body's own false-path
// logic (is_skill_offensive()/m_can_be_helpful_skills) does NOT reduce to
// the 2-arg body's false-path logic (AFK/looting/writing/level-range checks)
// for any substitute skill_id -- so a single hook fn-ptr cannot just forward
// a placeholder skill_id into the 3-arg real method. Instead: the fn-ptr
// mirrors the WIDER (3-arg) signature explicitly, and kNoSkillId is a
// sentinel (-1; every real skill id is >= 0 -- see spells.h's
// SKILL_BAREHANDED) that big_brother.cpp's registered implementation
// branches on to call the EXACT right real overload, not a lossy
// approximation of the 2-arg one. dispatch_target_valid()'s `skill_id`
// parameter defaults to kNoSkillId, so a clerics.cpp-shaped 2-arg call site
// reads as dispatch_target_valid(attacker, victim), and fight.cpp's 3-arg
// call site as dispatch_target_valid(attacker, victim, skill_id) -- both
// consumer-free when this hook pair was built (Task 2), converted for real
// in the combat-pilot wave's Task 3 (clerics.cpp) and Task 5 (fight.cpp).
// Unregistered default: LOGGED + return true (permissive legacy semantics --
// big_brother VETOES by returning false, so "no big brother installed" must
// default to "allow", the same neutral-default class as this header's
// float-returning hooks above, not the abort-on-unregistered class the
// txt-block-pool pair uses).
using target_valid_fn = bool (*)(char_data* attacker, const char_data* victim, int skill_id);
inline constexpr int kNoSkillId = -1;
void set_target_valid_hook(target_valid_fn hook);

// big_brother's death-notification edge: fight.cpp's upward
// game_rules::big_brother::instance().on_character_died() call. Mirrors
// big_brother.h's exact (dead_man, killer, corpse) signature -- no default
// parameter exists on the real method, unlike is_target_valid() above.
// Unregistered default: LOGGED no-op (void return, same class as
// set_attacked_player_hook() above).
using character_died_fn = void (*)(char_data* dead_man, char_data* killer, obj_data* corpse);
void set_character_died_hook(character_died_fn hook);

// Dispatch entry points for the two big_brother hooks above. Like
// dispatch_wild_attack_speed_multiplier()/dispatch_attacked_player() above,
// these are called from clerics.cpp/fight.cpp -- not entity_lifecycle.cpp,
// the TU that owns their backing storage -- so they need external-linkage
// declarations here. Defined in entity_lifecycle.cpp, next to their backing
// storage (this header's established convention regardless of which TU
// actually calls dispatch -- see dispatch_attacked_player()'s own comment).
bool dispatch_target_valid(char_data* attacker, const char_data* victim, int skill_id = kNoSkillId);
void dispatch_character_died(char_data* dead_man, char_data* killer, obj_data* corpse);

// char_from_room() (handler.cpp:349; behavior wave Task 1, census section
// 11): limits.cpp's future upward char_from_room(character) call sites
// (limits.cpp:582/:590, consumer-free this task) mirror this seam's
// existing extract_char_fn shape exactly -- char_from_room()'s own body
// calls stop_fighting() (fight.cpp, rots_combat, L3), which is what bars a
// clean L2 body relocation (census confirms zero rots_world/rots_entity
// caller, so the STOP-risk this hook's own census section flags does not
// fire). handler.cpp keeps the real body and registers it via
// register_char_from_room_hook() (handler.h/handler.cpp), the same
// convention as register_extract_char_hook() above. Tripwire default: a
// LOGGED no-op (void class, same taxonomy as set_attacked_player_hook()
// above -- no dereference risk on a void-returning hook).
using char_from_room_fn = void (*)(char_data* ch);
void set_char_from_room_hook(char_from_room_fn hook);
void dispatch_char_from_room(char_data* ch);

}
