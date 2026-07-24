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

#include "platdef.h"
#include <cstdlib>
#include <cstring>

#include "entity_hooks.h"
#include "handler.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "rots/platform/log.h"
#include "utils.h"
#include "zone.h" /* For zone_data's full definition -- zone_by_id() (entity_hooks.h)
                  * returns zone_data*, and char_to_room()/detach_char_from_room()
                  * (Task 3, below) dereference its ->zone/->white_power/->dark_power
                  * fields; zone_table[]/top_of_zone_table themselves stay untouched
                  * (rots_world, L3 -- only the resolver dispatch reaches them). */

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
    return ch->in_room;
}

void set_location(char_data* ch, int rnum)
{
    // List linkage (room->people / ch->next_in_room) stays the call sites'
    // responsibility this wave (spec Stage 1 scope) -- this wrapper only
    // assigns the scalar id, mirroring the field it replaces.
    ch->in_room = rnum;
}

bool is_in_room(const char_data* ch, int rnum)
{
    return ch->in_room == rnum;
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

namespace rots::entity {

// Range-for-capable wrapper over a room's intrusive occupant chain
// (room_data::people / char_data::next_in_room walk) -- the spec's
// occupants(room) Stage-1 API entry. Unused as landed: char_power's/
// recount_light_room's/get_char_room's own occupant walks stayed verbatim
// inline loops (per the brief's "MOVE verbatim" -- only their world[]
// accesses became room_by_id() calls), and Task 3's
// detach_char_from_room()/char_to_room() primitives (placement.cpp) also
// landed with their own verbatim inline next_in_room loops rather than
// this range (see those functions below). Kept as an unused Stage-1 API
// surface -- not deleted -- pending the game-wide call-site-conversion /
// Stage-2 wave that would migrate existing next_in_room walks onto it.
class occupant_range {
public:
    // Minimal forward iterator over the next_in_room chain -- only the
    // operations range-for needs (dereference, prefix increment,
    // inequality). No existing range/iterator idiom lives elsewhere in
    // rots_entity to extend instead, per the brief's precedent search.
    class iterator {
    public:
        // Current chain node; nullptr is the end-of-chain sentinel,
        // mirroring the legacy `for (; tmpch; tmpch = tmpch->next_in_room)`
        // walks' own null-terminated-list convention.
        explicit iterator(char_data* node)
            : node_(node)
        {
        }

        char_data* operator*() const { return node_; }

        iterator& operator++()
        {
            node_ = node_->next_in_room;
            return *this;
        }

        bool operator!=(const iterator& other) const
        {
            return node_ != other.node_;
        }

    private:
        // Chain node this iterator currently refers to.
        char_data* node_;
    };

    // Snapshots room's occupant-chain head at construction time (matching
    // the legacy walks' own single-read-then-follow-next behavior); a null
    // room yields an empty range.
    explicit occupant_range(room_data* room)
        : first_(room ? room->people : nullptr)
    {
    }

    iterator begin() const { return iterator(first_); }
    iterator end() const { return iterator(nullptr); }

private:
    // Head of the walked chain at construction time; iteration follows
    // next_in_room from here.
    char_data* first_;
};

// Returns a range-for-capable view of room's occupants. L1 field wrapper --
// no hook needed, see occupant_range's own comment.
inline occupant_range occupants(room_data* room)
{
    return occupant_range(room);
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
    for (tmpch = r->people; tmpch; tmpch = tmpch->next_in_room)
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

    for (i = room_by_id_total(room)->people, j = 1; i && (j <= number); i = i->next_in_room)
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

    room_data* r = room_by_id_total(room);

    /* append ch to the room's list */
    if (!r->people)
        r->people = ch;
    else {
        for (tmpch = r->people; tmpch->next_in_room; tmpch = tmpch->next_in_room)
            ;
        tmpch->next_in_room = ch;
    }
    ch->next_in_room = 0;
    ch->in_room = room;

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
    if (ch->in_room == NOWHERE) {
        //      log("SYSERR: NOWHERE extracting char from room (handler.c, char_from_room)");
        //      exit(1);
        return false; // he's already nowehre
    }

    room_data* r = room_by_id_total(ch->in_room);

    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                if (ch->equipment[tmp]->obj_flags.value[2] && (ch->equipment[tmp]->obj_flags.value[3])) /* Light is ON */
                    r->light--;

    if (ch == r->people) /* head of list */
        r->people = ch->next_in_room;

    else /* locate the previous element */ {
        for (i = r->people;
             i && (i->next_in_room != ch); i = i->next_in_room)
            ;

        if (!i)
            return false;

        i->next_in_room = ch->next_in_room;
    }

    tmp = char_power(GET_LEVEL(ch));

    if (!IS_NPC(ch)) {
        //     zone_table[world[ch->in_room].zone].nature_power -= tmp;
        if (RACE_GOOD(ch))
            zone_by_id(r->zone)->white_power -= tmp;
        else if (RACE_EVIL(ch))
            zone_by_id(r->zone)->dark_power -= tmp;
    }

    ch->in_room = NOWHERE;
    ch->next_in_room = 0;
    return true;
}
