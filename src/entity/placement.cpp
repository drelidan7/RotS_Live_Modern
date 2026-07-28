/* placement.cc */

// New TU (placement-seam wave, Task 1; plan
// docs/superpowers/plans/2026-07-19-placement-seam.md; spec
// docs/superpowers/specs/2026-07-19-placement-seam-design.md; census
// .superpowers/sdd/placement-census.md). Seam foundation for the
// placement/equipment carve out of handler.cpp/utility.cpp: this file joins
// ROTS_ENTITY_SOURCES (rots_entity, L2) and owns (a) the three
// hook-dispatched world-id resolvers (room_by_id/zone_by_id/
// obj_index_by_id -- world[]/zone_table/obj_index live in rots_world, L3,
// so L2 reaches them only through entity_hooks.h's registered function
// pointers, mirroring entity_lifecycle.cpp's existing hook precedent), (b)
// thin L1-field wrappers over char_data/room_data (location_of/
// set_location/is_in_room/occupants -- both are L1 core structs, so once an
// id is resolved to a pointer, field access needs no hook), and (c) the
// first three census-classified functions relocated verbatim from
// handler.cpp: char_power (:126), recount_light_room (:140), get_char_room
// (:1164). Declarations for all three stay in handler.h, unchanged.
//
// Controller-adjudicated deviations from the Task 1 brief (both STOP-flagged
// during implementation; see task-1-report.md for the full evidence trail
// and adjudication):
//
// 1. recount_light_room's original bounds check (`if (room < 0 || room >=
//    top_of_world) return;`) read top_of_world, an extern int defined in
//    db_world.cpp (rots_world, L3) -- an upward edge the census did not
//    list (only world[] was listed for this function). Resolution:
//    room_by_id (and, for symmetry, zone_by_id/obj_index_by_id) now carries
//    a BOUNDS-CHECKED CONTRACT -- the REGISTERED implementation itself
//    returns nullptr for an out-of-range id (see entity_hooks.h's fuller
//    contract comment) -- so recount_light_room's moved body below replaces
//    its inline bounds check with a null check on room_by_id()'s result
//    instead of reading top_of_world directly. Byte-identical in-range
//    behavior; the early-return-on-out-of-range outcome is preserved too
//    (just reached via a null pointer instead of a direct integer compare).
// 2. get_char_room calls get_number(), which was still defined in
//    handler.cpp (app tier) as of this task's start -- the census correctly
//    classifies get_number as MOVE-OTHER(platform), but scheduled for
//    Task 4, after this task's own get_char_room move. Resolution:
//    get_number moves to rots_util.cpp (rots_platform, L0) THIS task
//    instead (a sequencing fix, not a scope change -- see rots_util.cpp's
//    own relocation comment); Task 4's platform batch shrinks by this one
//    function accordingly.
//
// Post-review follow-up (same task, reviewer-corrected): room_data::
// operator[] (world[]'s actual indexing operator, db_world.cpp) is a
// graceful TOTAL function -- out-of-range input logs and returns a
// fallback room, not undefined behavior -- so a single nullptr-on-invalid
// room_by_id() would have silently narrowed behavior for callers that
// historically indexed world[] unchecked. room_by_id_total() (below)
// restores that exact historical fallback for get_char_room, which is
// one such caller; room_by_id() keeps its nullptr contract for callers
// (recount_light_room) that historically bounds-checked instead. See
// entity_hooks.h's contract comment and task-1-report.md for the full
// two-variant rationale, including why zone_by_id/obj_index_by_id did
// NOT need a total counterpart (raw arrays, no fallback wrapper -- their
// unchecked historical behavior was genuine UB, so nullptr is a strict
// improvement there rather than a narrowing).

//
// ==========================================================================
// THE LOCATION INVARIANT (LS-3b T5 -- the store split; owner ruling O-5,
// controller ruling R-3b-A). This file is the LocationSystem: it owns
// char_data::ls_location_id_ (the location), char_data::ls_next_in_room_ /
// room_data::ls_first_occupant_ (the occupant chain that is the store's own
// private implementation), and char_special_data::ls_load_room_vnum_ (the
// persisted-VNUM channel's separate storage). Nothing outside this file,
// containment.cpp, handler.h's occupant_range/first_occupant API bodies and
// src/tests/test_placement.h may name those members; everything else goes
// through location_of()/set_location()/room_of()/occupants()/
// first_occupant()/is_in_room()/stash_load_room_vnum()/
// peek_load_room_vnum().
//
// THE INVARIANT, stated so it can be enforced and tested:
//
//   location_of(ch) == NOWHERE  ==>  ch is linked into NO room's occupant
//                                    chain, anywhere in the world.
//   location_of(ch) == r (a room id)
//                              ==>  ch is linked into exactly world[r]'s
//                                   chain, and into no other.
//
// DIRECTION (binding, from the settled torn-state investigation): the chain
// FOLLOWS the field, never the other way round. char_to_room() and
// detach_char_from_room() below are the only functions that link or unlink,
// and each writes the field and the chain together.
//
// WHAT MAKES THE FIRST HALF ESTABLISHABLE (owner ruling O-5, the narrow
// amendment to strict equivalence): char_to_room(ch, NOWHERE) no longer
// splices ch into room 0 (room_data::operator[]'s out-of-range fallback) and
// no longer bumps that room's light or its zone's power counters. NOWHERE
// now means what it says -- linked nowhere. Before this commit the call left
// a TORN state (chain said room 0, field said nowhere) that made
// detach_char_from_room()'s early return leak a chain entry, let
// extract_char() free a still-linked character (census B's D5), and let a
// re-placement truncate room 0's chain (census B's D6/S8).
//
// THE ONE EXCEPTION, and why it cannot break the half that matters:
// ScopedRenderLocation (the cursor family, LS-3b T2) deliberately spoofs the
// field for the duration of a render window without moving the character,
// so the SECOND half above does not hold inside such a window. It spoofs a
// REAL room id and never NOWHERE, so the FIRST half -- the one every
// absence guard, every teardown path and this file's own early returns rely
// on -- holds unconditionally.
// ==========================================================================

#include "platdef.h"
#include <cstdlib>
#include <cstring>

#include "entity_hooks.h"
#include "handler.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "rots/entity/render_cursor.h"
#include "rots/platform/log.h"
#include "utils.h"
#include "zone.h" /* For zone_data's full definition -- zone_by_id() (entity_hooks.h)
                  * returns zone_data*, and char_to_room()/detach_char_from_room()
                  * (Task 3, below) dereference its ->zone/->white_power/->dark_power
                  * fields; zone_table[]/top_of_zone_table themselves stay untouched
                  * (rots_world, L3 -- only the resolver dispatch reaches them). */

namespace {
// ==========================================================================
// THE COMPILE-TIME ABSENCE ASSERTION (LS-3b T8) -- the mechanical witness for
// corrected criterion (a)'s first clause: "char_data carries no location
// field [observable outside the placement core]" (R-3b-A's private-handle
// reading; see the invariant block above). tools/location_read_census.py can
// only see PRESENCE -- its tracked tokens flag a raw spelling wherever one
// exists. It structurally cannot see an ABSENCE, so nothing in that gate can
// prove the OLD public members are actually gone rather than merely unused.
// This block is that proof, expressed so a future reintroduction fails the
// BUILD rather than merely tripping a grep line.
//
// A C++20 requires-expression is SFINAE-friendly by construction: when the
// named member does not exist on T, the expression inside the braces is
// simply ill-formed and the requires-expression evaluates to false -- never
// a hard compile error. That is exactly the property this witness needs:
// gracefully false while the field stays privatized (today's, correct,
// state), loud -- a static_assert failure, i.e. a build break -- the instant
// a future edit reintroduces the old public spelling.
//
// Housed HERE, not in a standalone test TU, for two reasons: (1)
// src/entity/placement.cpp is the representation owner, a whole-file
// LS1-ALLOW exemption (docs/superpowers/location-read-allowlist.md), so the
// literal `.in_room`/`.next_in_room`/`.people` spellings the detection idiom
// below must write to probe for the member need no per-line annotation --
// this file's exemption already covers them; (2) this TU is part of
// ROTS_ENTITY_SOURCES, an always-built target on every preset (host,
// container, MSVC, sanitizer) -- unlike an opt-in ctest executable, this
// witness can never be silently skipped by a target going unbuilt or a
// `-DBUILD_TESTING=OFF` configure.
template <typename T>
concept HasPublicInRoomMember = requires(T instance) { instance.in_room; };

template <typename T>
concept HasPublicNextInRoomMember = requires(T instance) { instance.next_in_room; };

template <typename T>
concept HasPublicPeopleMember = requires(T instance) { instance.people; };

static_assert(!HasPublicInRoomMember<char_data>,
    "char_data::in_room has been reintroduced. LS-3b T5 privatized this field behind "
    "char_data::ls_location_id_ (LocationSystem's private handle, R-3b-A) -- route through "
    "location_of()/set_location() instead of restoring the public member.");
static_assert(!HasPublicNextInRoomMember<char_data>,
    "char_data::next_in_room has been reintroduced. LS-3b T5 privatized this field behind "
    "char_data::ls_next_in_room_ -- route through the Stage-1 occupant API "
    "(occupants()/first_occupant()) instead of restoring the public member.");
static_assert(!HasPublicPeopleMember<room_data>,
    "room_data::people has been reintroduced. LS-3b T5 privatized this field behind "
    "room_data::ls_first_occupant_ -- route through first_occupant()/occupants() instead of "
    "restoring the public member.");
} // namespace
// ==========================================================================

namespace rots::entity {

namespace {
// Backing storage for the registered world-id resolver hooks
// (register_world_resolver_hooks(), db_world.cpp/zone_load.cpp -- see
// entity_hooks.h). Null until that registration runs; an UNREGISTERED hook
// is a hard failure (loud log + abort, dispatched below) distinct from a
// REGISTERED hook returning nullptr (room_by_id/zone_by_id/
// obj_index_by_id) or a fallback room (room_by_id_total) for an
// out-of-range id, which is a normal result -- see entity_hooks.h's
// contract comment for the full rationale (mirrors this header's existing
// txt-block-pool pair's "no safe placeholder pointer" reasoning).
room_resolver_fn g_room_resolver_hook = nullptr;
room_total_resolver_fn g_room_total_resolver_hook = nullptr;
zone_resolver_fn g_zone_resolver_hook = nullptr;
obj_index_resolver_fn g_obj_index_resolver_hook = nullptr;
} // namespace

void set_room_resolver_hook(room_resolver_fn hook)
{
    g_room_resolver_hook = hook;
}

void set_room_total_resolver_hook(room_total_resolver_fn hook)
{
    g_room_total_resolver_hook = hook;
}

void set_zone_resolver_hook(zone_resolver_fn hook)
{
    g_zone_resolver_hook = hook;
}

void set_obj_index_resolver_hook(obj_index_resolver_fn hook)
{
    g_obj_index_resolver_hook = hook;
}

} // namespace rots::entity

/************************************************************************
 *  Stage-1 API: id -> pointer resolvers (spec's location-representation *
 *  section -- "room ids are the public currency; room_by_id(id) is the  *
 *  explicit escalation to a room_data*"). Global functions, declared in *
 *  handler.h; every moved function below reaches world[]/zone_table/    *
 *  obj_index only through these four.                                   *
 ************************************************************************/

room_data* room_by_id(int rnum)
{
    if (rots::entity::g_room_resolver_hook) {
        return rots::entity::g_room_resolver_hook(rnum);
    }
    rots::log::write_stderr(
        "rots::entity: FATAL room resolver hook called with no sink registered -- this should be "
        "unreachable once register_world_resolver_hooks() has run.");
    abort();
}

// TOTAL counterpart to room_by_id() above -- see entity_hooks.h's
// two-variant contract comment. For callers whose ORIGINAL code indexed
// world[x] unchecked (no bounds test of their own): preserves
// room_data::operator[]'s exact historical fallback-room + mudlog
// behavior for every input, in range or not, rather than room_by_id()'s
// narrower nullptr-on-invalid contract.
room_data* room_by_id_total(int rnum)
{
    if (rots::entity::g_room_total_resolver_hook) {
        return rots::entity::g_room_total_resolver_hook(rnum);
    }
    rots::log::write_stderr(
        "rots::entity: FATAL room-total resolver hook called with no sink registered -- this "
        "should be unreachable once register_world_resolver_hooks() has run.");
    abort();
}

zone_data* zone_by_id(int znum)
{
    if (rots::entity::g_zone_resolver_hook) {
        return rots::entity::g_zone_resolver_hook(znum);
    }
    rots::log::write_stderr(
        "rots::entity: FATAL zone resolver hook called with no sink registered -- this should be "
        "unreachable once register_world_resolver_hooks() has run.");
    abort();
}

index_data* obj_index_by_id(int item_number)
{
    if (rots::entity::g_obj_index_resolver_hook) {
        return rots::entity::g_obj_index_resolver_hook(item_number);
    }
    rots::log::write_stderr(
        "rots::entity: FATAL obj-index resolver hook called with no sink registered -- this "
        "should be unreachable once register_world_resolver_hooks() has run.");
    abort();
}

/************************************************************************
 *  Stage-1 API: thin L1-field wrappers (no hook -- char_data/room_data   *
 *  are L1 core structs, so once an id is resolved, field access is a    *
 *  legal downward reference on its own).                                 *
 ************************************************************************/

int location_of(const char_data* ch)
{
    return ch->ls_location_id_;
}

void set_location(char_data* ch, int rnum)
{
    // A BARE FIELD WRITE, and deliberately still one after the store split:
    // this is the render-cursor primitive (ScopedRenderLocation) and the
    // scratch/sentinel writer, not a relocation. Linking and unlinking are
    // char_to_room()/detach_char_from_room()'s job -- see the invariant
    // block at the top of this file for why writing a REAL room id here
    // (the cursor case) cannot break the absence half of the invariant.
    ch->ls_location_id_ = rnum;
}

bool is_in_room(const char_data* ch, int rnum)
{
    return ch->ls_location_id_ == rnum;
}

// The VNUM channel (LS-3a Wave T2 tranche 2e; rulings R-A2 / AM-1 /
// R-T0b-3) -- see handler.h for the full account of the overload these two
// names discriminate. THE STORE SPLIT LANDED HERE (LS-3b T5): the channel
// now has its own storage, char_special_data::ls_load_room_vnum_ -- the
// NON-persisted sibling of char_special2_data (census D section 4), so no
// save format moved -- and stops aliasing the location field
// location_of()/set_location() wrap. NO CALL SITE CHANGED: that was the
// whole point of naming the channel separately in LS-3a.
//
// WHAT THE SPLIT FIXES, in one sentence: a character sitting at the
// selection menu, or anywhere in the login/rent window, used to have the
// persisted room VNUM sitting in the field every absence guard reads, so
// location_of() answered with a plausible-looking room id for somebody who
// is in no room at all. Now it answers NOWHERE (see the three in-range
// leaks this closes: comm.cpp's msdp_update(), protocol.cpp's
// broadcast_weather_msdp_update(), comm.cpp's clean_expose_elements()).
//
// NOWHERE means "nothing stashed" -- char_special_data's own initializer
// establishes that at every birth, so peek() is total for a character that
// never went through store_to_char().
//
// Bare field access on both sides: no occupant-chain, light or zone-power
// bookkeeping (the stash runs at points where the character is deliberately
// in no room at all), and no resolver hook, so both stay rots_convert-safe.
void stash_load_room_vnum(char_data* ch, int vnum)
{
    ch->specials.ls_load_room_vnum_ = vnum;
}

int peek_load_room_vnum(const char_data* ch)
{
    return ch->specials.ls_load_room_vnum_;
}

// Self-room convenience (LS-1 Wave Task 1; .superpowers/sdd/ls1-census.md
// Step 5 -- census-justified by ~161 counted self-room
// `world[X->in_room]` read sites the wave's T2 conversions collapse onto
// this call). Thin wrapper folding the ch->in_room read and the world[]
// resolve into one call; never null -- preserves room_by_id_total()'s
// graceful out-of-range fallback rather than room_by_id()'s nullptr
// contract, matching every existing self-room site's historical
// behavior. Consumer-free as landed -- T2's conversions are the first
// callers.
room_data* room_of(const char_data* ch)
{
    return room_by_id_total(location_of(ch));
}

// occupant_range/const_occupant_range/occupants() (both overloads) moved
// verbatim into handler.h (LS-1 Wave Task 1b; .superpowers/sdd/
// ls1-task-1b-report.md) so every TU reachable through handler.h can
// call occupants() -- previously TU-local to this file, per Stage-1's
// own "Unused as landed" comment history. No duplicate definition
// remains here; this file still includes handler.h (above) so its own
// callers below are unaffected.

/************************************************************************
 *  LS-3b Wave T2 (ruling R-3b-B): ScopedRenderLocation, the cursor-family *
 *  API. Contract lives in rots/entity/render_cursor.h -- this is a thin  *
 *  RAII wrapper over the same location_of()/set_location() pair the      *
 *  manual save/write/restore idiom already calls, so it inherits their   *
 *  exact behavior (bare field access pre-swap; T5's internal store swap  *
 *  is transparent here since neither function's signature or contract   *
 *  changes).                                                             *
 ************************************************************************/

namespace rots::entity {

ScopedRenderLocation::ScopedRenderLocation(char_data* ch, int room_id)
    : ch_(ch)
    , saved_location_(location_of(ch))
    , restored_(false)
{
    set_location(ch_, room_id);
}

ScopedRenderLocation::~ScopedRenderLocation()
{
    restore();
}

void ScopedRenderLocation::retarget(int room_id)
{
    set_location(ch_, room_id);
}

void ScopedRenderLocation::restore()
{
    if (restored_) {
        return;
    }
    set_location(ch_, saved_location_);
    restored_ = true;
}

} // namespace rots::entity

/************************************************************************
 *  Functions relocated verbatim from handler.cpp (placement-seam Task 1; *
 *  see census rows char_power:126, recount_light_room:140,               *
 *  get_char_room:1164). Bodies are byte-identical to their handler.cpp   *
 *  originals except each function's world[] accesses, replaced by a      *
 *  room_by_id() call/result per the census's SEAM disposition -- see     *
 *  task-1-report.md for the exact before/after quotes.                   *
 ************************************************************************/

int char_power(int lev)
{
    if (lev >= LEVEL_IMMORT)
        return 0;

    return MIN((lev + 2), 16 + lev / 2) * MIN(lev + 2, 32);
}

void recount_light_room(int room)
{
    struct char_data* tmpch;
    struct obj_data* tmpobj;
    int count, tmp;

    room_data* r = room_by_id(room);
    if (!r)
        return;

    count = 0;
    for (tmpch = r->ls_first_occupant_; tmpch; tmpch = tmpch->ls_next_in_room_)
        for (tmp = 0; tmp < MAX_WEAR; tmp++)
            if (tmpch->equipment[tmp])
                if (tmpch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                    if ((tmpch->equipment[tmp]->obj_flags.value[2] != 0) && (tmpch->equipment[tmp]->obj_flags.value[3] != 0))
                        count++;

    for (tmpobj = r->contents; tmpobj; tmpobj = tmpobj->next_content)
        if ((tmpobj->obj_flags.value[2] != 0) && (tmpobj->obj_flags.value[3] != 0))
            count++;

    r->light = count;
}

/* search a room for a char, and return a pointer if found..  */
struct char_data* get_char_room(char* name, int room)
{
    struct char_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = room_by_id_total(room)->ls_first_occupant_, j = 1; i && (j <= number); i = i->ls_next_in_room_)
        if (isname_nullable(tmp, i->player.name)) {
            if (j == number)
                return (i);
            j++;
        }

    return (0);
}

/************************************************************************
 *  Functions relocated/split from handler.cpp (placement-seam Task 3;   *
 *  see census rows char_to_room:716 (ADJUDICATE-1) and char_from_room:  *
 *  661 (ADJUDICATE-2), and task-3-report.md for the full evidence       *
 *  trail). char_to_room() is a clean MOVE (no output/combat weld);      *
 *  detach_char_from_room() is the SPLIT primitive half of               *
 *  char_from_room() -- handler.cpp's app-side char_from_room() wrapper  *
 *  (public name/declaration unchanged) calls this then runs the         *
 *  original's trailing stop_fighting teardown loop (combat/app).        *
 ************************************************************************/

/* place a character in a room */
//
// world[room] substitution (census row char_to_room:716; ADJUDICATE-1
// Disposition A -- binding): the ORIGINAL body indexed world[room]
// unchecked at every site below (no bounds test anywhere in the function)
// -- per the BINDING addendum's per-site selection rule, this resolves via
// room_by_id_total(room) into a single local `room_data* r`, mirroring
// recount_light_room's/get_char_room's own precedent for hoisting the
// resolved pointer once at entry. `world[room].people`/`.light` below
// become `r->people`/`.light`. zone_table[world[room].zone] becomes
// zone_by_id(r->zone) -- the new zone resolver ADJUDICATE-1 introduced;
// zone_by_id() is a raw-array nullptr-on-invalid resolver (no TOTAL
// counterpart needed) and r->zone is always an in-range zone index set at
// world-load time, so it is dereferenced directly, per the BINDING
// addendum's "no NEW null checks" rule for zone_by_id/obj_index_by_id.
void char_to_room(struct char_data* ch, int room)
{
    struct char_data* tmpch;
    int tmp;

    // THE RESOLVER CALL IS PRESERVED UNCONDITIONALLY (census B section 2.4/S4
    // as restated by review finding F17 -- binding). It is made here, once,
    // for every argument including NOWHERE, exactly as before the O-5
    // amendment below. BOTH of its side effects are load-bearing and
    // neither may be short-circuited: room_data::operator[] mudlogs
    // "world[] called for negative room number." at LEVEL_GOD for i < 0
    // (db_world.cpp), and it abort()s when BASE_WORLD is unallocated, which
    // is what makes a fixture with no ScopedTestWorld fail loudly instead of
    // quietly passing. "Optimising away the resolve for the NOWHERE case"
    // would silently drop an operator-visible god-channel line and a
    // process-level tripwire that nobody asked to lose.
    room_data* r = room_by_id_total(room);

    // THE O-5 AMENDMENT (owner ruling, 2026-07-28; the single narrow
    // amendment to strict equivalence this wave carries). NOWHERE means
    // LINKED NOWHERE: no splice into the fallback room's occupant chain, no
    // light bump, no zone-power bump. Only the location field is written,
    // and it is written to the ORIGINAL argument, exactly as the unamended
    // body did.
    //
    // WHAT IS DELIBERATELY *NOT* DONE HERE, and why. The unamended body ran
    // `ch->next_in_room = 0;` on every path, including this one. That write
    // is census B's S8 chain-truncation hazard: for a character who is still
    // linked into some room's chain, zeroing the forward link orphans every
    // occupant behind it. Under the invariant at the top of this file the
    // NOWHERE path must not touch a chain at all, so the write does not
    // survive here in any form. (For a character who is genuinely unlinked
    // the write was a no-op anyway -- it is not a lost hygiene step.)
    //
    // The flagged, tested observable deltas this produces are census B's
    // D1 (room-0 occupant walkers no longer see the character), D2 (room 0's
    // light no longer gains a permanent leaked +1) and D3 (the zone-power
    // spike, wiped within ~60s by recalc_zone_power's absolute rebuild
    // either way). What it retires is D5 (extract_char freeing a character
    // still spliced into room 0 -- a use-after-free) and D6/S8 above.
    if (room == NOWHERE) {
        ch->ls_location_id_ = room;
        return;
    }

    /* append ch to the room's list */
    if (!r->ls_first_occupant_)
        r->ls_first_occupant_ = ch;
    else {
        for (tmpch = r->ls_first_occupant_; tmpch->ls_next_in_room_; tmpch = tmpch->ls_next_in_room_)
            ;
        tmpch->ls_next_in_room_ = ch;
    }
    ch->ls_next_in_room_ = 0;
    ch->ls_location_id_ = room;

    /* do they have a light? */
    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                if (ch->equipment[tmp]->obj_flags.value[2] && (ch->equipment[tmp]->obj_flags.value[3])) /* Light is ON */
                    r->light++;

    tmp = char_power(GET_LEVEL(ch));

    /* increase the goodness/evilness of this room's zone */
    if (!IS_NPC(ch)) {
        if (RACE_GOOD(ch))
            zone_by_id(r->zone)->white_power += tmp;
        else if (RACE_EVIL(ch))
            zone_by_id(r->zone)->dark_power += tmp;
    }
}

/* move a player out of a room */
//
// detach_char_from_room() SPLIT primitive (census row char_from_room:661;
// ADJUDICATE-2 Disposition A -- binding): everything through clearing
// in_room/next_in_room (list-unlink + light-dec + zone-power-dec) moved
// here; handler.cpp's char_from_room() app wrapper keeps the public
// name/declaration and runs the original's trailing stop_fighting loop
// (combat/app) after calling this. world[ch->in_room] substitution: the
// ORIGINAL body indexed world[ch->in_room] unchecked (no bounds test,
// distinct from the `ch->in_room == NOWHERE` sentinel check) -- per the
// BINDING addendum, room_by_id_total(ch->in_room) into a local
// `room_data* r`, same precedent as char_to_room() above.
// zone_table[world[ch->in_room].zone] becomes zone_by_id(r->zone),
// dereferenced directly (same rationale as char_to_room() above).
//
// bool return (task-3-report.md controller adjudication, supersedes this
// task's own original void signature -- a controller-authorized named
// deviation from the plan): the ORIGINAL char_from_room had two
// early-return paths that both skipped its trailing stop_fighting loop
// entirely -- (1) `ch->in_room == NOWHERE` and (2) the defensive `if (!i)
// return;` below. A void primitive gave the wrapper no way to tell these
// apart from "ran to completion" without re-deriving them itself, so this
// primitive now returns false on exactly those two original early-return
// paths and true after the (unchanged) full detach; the wrapper branches
// on that value instead of re-evaluating either condition -- see
// handler.cpp's char_from_room() wrapper for the exact mapping table.
bool detach_char_from_room(char_data* ch)
{
    struct char_data* i;
    int tmp;
    if (ch->ls_location_id_ == NOWHERE) {
        //      log("SYSERR: NOWHERE extracting char from room (handler.c, char_from_room)");
        //      exit(1);
        return false; // he's already nowehre
    }

    room_data* r = room_by_id_total(ch->ls_location_id_);

    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                if (ch->equipment[tmp]->obj_flags.value[2] && (ch->equipment[tmp]->obj_flags.value[3])) /* Light is ON */
                    r->light--;

    if (ch == r->ls_first_occupant_) /* head of list */
        r->ls_first_occupant_ = ch->ls_next_in_room_;

    else /* locate the previous element */ {
        for (i = r->ls_first_occupant_;
             i && (i->ls_next_in_room_ != ch); i = i->ls_next_in_room_)
            ;

        if (!i)
            return false;

        i->ls_next_in_room_ = ch->ls_next_in_room_;
    }

    tmp = char_power(GET_LEVEL(ch));

    if (!IS_NPC(ch)) {
        //     zone_table[world[ch->in_room].zone].nature_power -= tmp;
        if (RACE_GOOD(ch))
            zone_by_id(r->zone)->white_power -= tmp;
        else if (RACE_EVIL(ch))
            zone_by_id(r->zone)->dark_power -= tmp;
    }

    ch->ls_location_id_ = NOWHERE;
    ch->ls_next_in_room_ = 0;
    return true;
}

/* move every occupant of one room's chain onto another room's, in bulk */
//
// relocate_all_occupants() (LS-3a T2c; ruling R-A6, .superpowers/sdd/
// ls3a-global-constraints.md; census A sections 5.2/6.4) -- the occupant half
// of the bulk splice ferry_captain() (script/spec_pro.cpp) used to hand-roll
// inline, the only manual char-chain splice that was left in production,
// lifted here statement-for-statement. handler.h carries the full contract;
// these are the notes that matter to the BODY:
//
//  * NO RESOLVER HOISTING (LS-3a's standing constraint): every one of the
//    ORIGINAL's `world[old_room]`/`world[new_room]` accesses becomes exactly
//    one room_by_id_total() call, in the same order. room_data::operator[]
//    mudlogs for an out-of-range id, so collapsing N accesses into one hoisted
//    `room_data* r` -- the shape char_to_room()/detach_char_from_room() above
//    use, where the id is read once anyway -- would silently drop N-1 of those
//    side effects here. Assignment's RHS is sequenced before its LHS (C++17),
//    in the original exactly as here. PRECISION (LS-3a review F10): the
//    per-statement call MULTISET matches the original exactly (6 from / 6
//    to); the interleaved SEQUENCE differs where the original alternated
//    char and object statements that now live in the two split functions.
//    That difference is state-invisible (disjoint fields) and observable
//    only through operator[]'s out-of-range mudlog ORDERING when BOTH ids
//    are invalid -- unreachable from the sole caller, recorded honestly.
//  * The first walk BREAKS ON THE TAIL -- a peek-ahead that exits with tmpch
//    pointing AT the last node rather than past it. That is precisely why
//    census A section 6.4 ruled this block not convertible to occupants();
//    reproduced verbatim.
//  * The third walk re-stamps each node's location mid-walk, reading
//    next_in_room before the stamp -- also reproduced verbatim.
//  * ch->in_room is written directly rather than through set_location(): this
//    file IS the representation owner (docs/superpowers/
//    location-read-allowlist.md), and char_to_room() above writes the same
//    field the same way.
//  * Splitting the ORIGINAL's INTERLEAVED char/object statements into this
//    function and containment.cpp's relocate_all_contents() is behavior-
//    identical: the two halves touch disjoint fields (people/next_in_room/
//    char_data::in_room versus contents/next_content/obj_data::in_room) with
//    no aliasing between them, so neither half can observe the other's
//    ordering.
void relocate_all_occupants(int from_room, int to_room)
{
    struct char_data* tmpch;

    // The ONE addition to the lifted body, and unreachable from the sole
    // production caller -- ferry_captain() keeps its own
    // `if (new_room != old_room)` guard. Without it the walk below would link
    // the chain's own tail onto its own head (a cycle) and then null the room.
    if (from_room == to_room)
        return;

    for (tmpch = room_by_id_total(from_room)->ls_first_occupant_; tmpch; tmpch = tmpch->ls_next_in_room_) { // LS1-ALLOW: representation-impl
        if (!tmpch->ls_next_in_room_) // LS1-ALLOW: representation-impl
            break;
    }
    if (tmpch)
        tmpch->ls_next_in_room_ = room_by_id_total(to_room)->ls_first_occupant_; // LS1-ALLOW: representation-impl
    else
        room_by_id_total(from_room)->ls_first_occupant_ = room_by_id_total(to_room)->ls_first_occupant_; // LS1-ALLOW: representation-impl

    room_by_id_total(to_room)->ls_first_occupant_ = room_by_id_total(from_room)->ls_first_occupant_; // LS1-ALLOW: representation-impl
    room_by_id_total(from_room)->ls_first_occupant_ = 0; // LS1-ALLOW: representation-impl

    for (tmpch = room_by_id_total(to_room)->ls_first_occupant_; tmpch; tmpch = tmpch->ls_next_in_room_) { // LS1-ALLOW: representation-impl
        tmpch->ls_location_id_ = to_room; // LS1-ALLOW: representation-impl
    }
}
