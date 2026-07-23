/* zone_load.cc */

// Carved out of zone.cpp (world-seed wave, Task 4). Holds the zone-file
// parse/load half: the zone_load_cursor static (+ its explanatory comment),
// the TESTING seam reset_zone_load_cursor_for_testing(), load_zones(), and
// the renum_zone_table/renum_zone_one virtual-number-resolution pair --
// plus the zone_table/top_of_zone_table storage definitions (see the
// ownership comment directly above them). zone.cpp keeps the runtime half:
// zone_update() (comm heartbeat), check_if_flag(), reset_zone(), the
// reset_q pool, and is_empty() -- they keep reading zone_table/
// top_of_zone_table via zone.h's existing extern declarations, unchanged.
// Declarations for everything below stay in zone.h.
// The carved body was byte-identical to zone.cpp's prior lines 42-302 as
// carved (world-seed Task 4); Task 5's linkcheck cascade then converted the
// buf2 error labels in load_zones() to a local composer -- see the comment
// at that site for the disposition. (Note: the brief's
// evidence base cites the split boundary as "lines 48-302", i.e. the
// zone_load_cursor static itself; this carve also brings its immediately
// preceding six-line explanatory comment (source lines 42-47) along, since
// that comment explains why the storage is a hoisted static field named
// distinctly from `zone` -- leaving it behind in zone.cpp would orphan it
// next to code it no longer describes.)

#include <format>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h" /* For struct reset_com, real_mobile/real_room/real_object */
#include "utils.h" /* For CREATE/CREATE1/RECREATE + vmudlog */
#include "zone.h"

// zone.cpp -- struct zone_data* zone_table / int top_of_zone_table's
// DEFINITIONS move here (storage-placement only, mirroring
// entity_lifecycle.cpp's pulse/top_idnum precedent); zone.cpp's R half
// (zone_update()/check_if_flag()/reset_zone()/is_empty()) keeps
// reading/writing them via zone.h's existing extern declarations -- unlike
// the top_idnum precedent, zone.h already declared these extern before this
// split, so zone.cpp needed no new extern text of its own.
struct zone_data* zone_table;
int top_of_zone_table;

namespace rots::world {
// Resolver implementation for entity_hooks.h's zone resolver hook
// (placement-seam Task 1; declared in zone.h, registered by
// db_world.cpp's register_world_resolver_hooks()). placement.cpp's
// zone_by_id() (rots_entity) dispatches straight into this body through
// the registered function pointer -- db_world.cpp calling
// rots::entity::set_zone_resolver_hook(zone_by_id_impl) is a legal
// downward L3->L2 registration call.
//
// CONTRACT (controller-adjudicated, placement-seam Task 1; see
// entity_hooks.h's matching comment and task-1-report.md): bounds-
// checked, nullptr for znum outside [0, top_of_zone_table) -- see this
// function's own zone.h declaration comment for the boundary-symmetry
// caveat (no Task 1 caller exercises this resolver yet).
zone_data* zone_by_id_impl(int znum)
{
    if (znum < 0 || znum >= top_of_zone_table)
        return nullptr;
    return &zone_table[znum];
}
}

// Index of the next zone_table slot load_zones() will fill; incremented once
// per call. Historically a function-local `static int zone;` inside
// load_zones() — hoisted (same zero initialization, same lifetime, no other
// reader/writer) purely so the TESTING seam below can reset it between tests;
// named distinctly because renum_zone_one/reset_zone/check_if_flag all take a
// `zone` parameter that would otherwise shadow it.
static int zone_load_cursor;

#ifdef TESTING
// Test seam: resets the zone_load_cursor load_zones() advances on every call,
// so a test that loads a fixture zone into a 1-element zone_table doesn't leave
// the cursor pointing past that array for the next load_zones()-calling test
// (or a --gtest_repeat re-run) in the same monolithic test binary. Symbol does
// not exist in production builds (no -DTESTING there).
void reset_zone_load_cursor_for_testing()
{
    zone_load_cursor = 0;
}
#endif

/*
 * Given a cleanly opened zone file, load the zone information
 * such as coordinates, owners, description, etc. and load all of
 * the zone's commands.
 *
 * Replace the static 'zone' index with the top_of_zone_table.
 */
void load_zones(FILE* fl)
{
    int cmd_no;
    char buf[81], command;
    struct owner_list* owner;
    extern char* fread_string(FILE*, std::string_view);

    memset(&zone_table[zone_load_cursor], 0, sizeof(struct zone_data));
    fscanf(fl, " #%d\n", &zone_table[zone_load_cursor].number);
    // world-seed Task 5: local error_label replaces the former shared buf2
    // scratch-text global (mirrors db_world.cpp's load_rooms() error_label
    // precedent, world-seed Task 2) -- fread_string() takes a
    // std::string_view, so a local std::string composed once and reused for
    // each field below is a byte-identical drop-in; db_boot.cpp's buf2
    // storage is untouched.
    std::string error_label = std::format("beginning of zone #{}", zone_table[zone_load_cursor].number);

    zone_table[zone_load_cursor].name = fread_string(fl, error_label);
    zone_table[zone_load_cursor].description = fread_string(fl, error_label);
    zone_table[zone_load_cursor].map = fread_string(fl, error_label);

    /* Read in the owner list.  An owner of '0' ends the list. */
    CREATE1(zone_table[zone_load_cursor].owners, owner_list);
    owner = zone_table[zone_load_cursor].owners;
    for (;;) {
        fscanf(fl, "%d", &owner->owner);
        if (owner->owner) {
            CREATE1(owner->next, owner_list);
            owner = owner->next;
        } else
            break;
    }

    /* Eat up the rest of the line */
    while (fgetc(fl) != '\n')
        continue;
    fscanf(fl, "%c %d %d %d\n",
        &zone_table[zone_load_cursor].symbol,
        &zone_table[zone_load_cursor].x,
        &zone_table[zone_load_cursor].y,
        &zone_table[zone_load_cursor].level);
    fscanf(fl, "%d\n", &zone_table[zone_load_cursor].top);
    fscanf(fl, "%d\n", &zone_table[zone_load_cursor].lifespan);
    fscanf(fl, "%d\n", &zone_table[zone_load_cursor].reset_mode);

    /* Read the command list */
    for (cmd_no = 0;; ++cmd_no) {
        fscanf(fl, "%c", &command);

        /*
         * Marks the end of the zone command list
         * XXX: Question: if we originally allocated a command structure
         * even for 'S' commands (which are unused), then does other code
         * depend on finding 'S' commands to terminate the list?  We could
         * use the cmd_no data we have here to set a number in the zone
         * structure to tell us how many commands there are, so we don't
         * NEED the terminating 'S' command.
         */
        if (command == 'S') {
            vmudlog(CMP, "Encountered S command on command number #%d.",
                cmd_no);
            break;
        }

        if (!cmd_no)
            CREATE(zone_table[zone_load_cursor].cmd, struct reset_com, 1);
        else {
            RECREATE(zone_table[zone_load_cursor].cmd, struct reset_com, cmd_no + 1, cmd_no);
            if (!(zone_table[zone_load_cursor].cmd)) {
                perror("reset command load");
                exit(0);
            }
        }

        /* XXX: still preserving the 'S' command */
        zone_table[zone_load_cursor].cmd[cmd_no].command = command;

        // if_flag/arg1..arg7 are declared `int` in struct reset_com (db.h), not `short`;
        // %hd here was writing through an `int*` as if it were a `short*` (undefined
        // behavior, and on real little-endian platforms silently truncating each write
        // to the low 2 bytes). shapezon.cpp's write side (write_zone/DIGITCHANGE) already
        // round-trips these fields with %d, so %d is what matches the declared type and
        // the on-disk textual format -- this is a boot-time-only in-memory struct
        // (reset_com is never persisted in binary form), so aligning the read format to
        // the field type changes no on-disk world-file format.
        fscanf(fl, "%d %d %d %d %d %d",
            &zone_table[zone_load_cursor].cmd[cmd_no].if_flag,
            &zone_table[zone_load_cursor].cmd[cmd_no].arg1,
            &zone_table[zone_load_cursor].cmd[cmd_no].arg2,
            &zone_table[zone_load_cursor].cmd[cmd_no].arg3,
            &zone_table[zone_load_cursor].cmd[cmd_no].arg4,
            &zone_table[zone_load_cursor].cmd[cmd_no].arg5);

        zone_table[zone_load_cursor].cmd[cmd_no].existing = 0;

        switch (zone_table[zone_load_cursor].cmd[cmd_no].command) {
        case 'M':
        case 'N':
        case 'X':
        case 'H':
        case 'E':
        case 'K':
        case 'Q':
            fscanf(fl, "%d %d",
                &zone_table[zone_load_cursor].cmd[cmd_no].arg6,
                &zone_table[zone_load_cursor].cmd[cmd_no].arg7);
            break;
        case 'P':
            fscanf(fl, "%d", &zone_table[zone_load_cursor].cmd[cmd_no].arg6);
        default:
            break;
        }

        /*
         * Read in the comment.
         * XXX: The comment is only saved to the zone structure in
         * shapezon.cc.  This *is* somewhat efficient, since we don't
         * need zone comments unless someone's actually looking at
         * them . . but that won't be very general.  We should save
         * the comment here.
         */
        fgets(buf, 80, fl);
        vmudlog(NRM, "Got command: %c %d %d %d %d %d %d %d.",
            zone_table[zone_load_cursor].cmd[cmd_no].command,
            zone_table[zone_load_cursor].cmd[cmd_no].arg1,
            zone_table[zone_load_cursor].cmd[cmd_no].arg2,
            zone_table[zone_load_cursor].cmd[cmd_no].arg3,
            zone_table[zone_load_cursor].cmd[cmd_no].arg4,
            zone_table[zone_load_cursor].cmd[cmd_no].arg5,
            zone_table[zone_load_cursor].cmd[cmd_no].arg6,
            zone_table[zone_load_cursor].cmd[cmd_no].arg7);
    }
    zone_table[zone_load_cursor].cmdno = cmd_no;
    zone_load_cursor++;

    top_of_zone_table = zone_load_cursor - 1;
}

/*
 * Renumber the entire zone table
 */
void renum_zone_table(void)
{
    int zone;
    void renum_zone_one(int);

    for (zone = 0; zone <= top_of_zone_table; zone++)
        renum_zone_one(zone);
}

/*
 * Renumber all virtual numbers referring to objects, mobiles,
 * rooms, etc. to reflect the real numbers of the corresponding
 * data.  The real numbers are the real indexes of the objects,
 * mobiles, etc. in their tables.
 */
void renum_zone_one(int zone)
{
    int comm, a, b;

    for (comm = 0; comm < zone_table[zone].cmdno; comm++) {
        vmudlog(CMP, "Doing renum_zone_one on command #%d.", comm);
        a = b = 0;

        switch (zone_table[zone].cmd[comm].command) {
        case 'A':
            switch (zone_table[zone].cmd[comm].arg1) {
            case 0:
            case 4:
            case 5:
            case 6:
                a = zone_table[zone].cmd[comm].arg3 = real_mobile(zone_table[zone].cmd[comm].arg3);
                break;
            }
            break;
        case 'L':
            zone_table[zone].cmd[comm].arg2 = real_room(zone_table[zone].cmd[comm].arg2);
            switch (zone_table[zone].cmd[comm].arg1) {
            case 0:
            case 5:
            case 6:
                a = zone_table[zone].cmd[comm].arg3 = real_mobile(zone_table[zone].cmd[comm].arg3);
                break;
            case 1:
            case 2:
            case 3:
                a = zone_table[zone].cmd[comm].arg3 = real_object(zone_table[zone].cmd[comm].arg3);
                break;
            }
            break;
        case 'M':
            a = zone_table[zone].cmd[comm].arg1 = real_mobile(zone_table[zone].cmd[comm].arg1);
            b = zone_table[zone].cmd[comm].arg2 = real_room(zone_table[zone].cmd[comm].arg2);
            break;
        case 'O':
            a = zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            if (zone_table[zone].cmd[comm].arg2 != NOWHERE)
                b = zone_table[zone].cmd[comm].arg2 = real_room(zone_table[zone].cmd[comm].arg2);
            break;
        case 'G':
            a = zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            break;
        case 'E':
            a = zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            break;
        case 'P': /* room and obj_to can be null, then load to last_obj */
            zone_table[zone].cmd[comm].arg1 = real_room(zone_table[zone].cmd[comm].arg1);
            a = zone_table[zone].cmd[comm].arg2 = real_object(zone_table[zone].cmd[comm].arg2);
            zone_table[zone].cmd[comm].arg3 = real_object(zone_table[zone].cmd[comm].arg3);
            break;
        case 'K':
            if (zone_table[zone].cmd[comm].arg1)
                zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            zone_table[zone].cmd[comm].arg2 = real_object(zone_table[zone].cmd[comm].arg2);
            zone_table[zone].cmd[comm].arg3 = real_object(zone_table[zone].cmd[comm].arg3);
            zone_table[zone].cmd[comm].arg4 = real_object(zone_table[zone].cmd[comm].arg4);
            zone_table[zone].cmd[comm].arg5 = real_object(zone_table[zone].cmd[comm].arg5);
            zone_table[zone].cmd[comm].arg6 = real_object(zone_table[zone].cmd[comm].arg6);
            zone_table[zone].cmd[comm].arg7 = real_object(zone_table[zone].cmd[comm].arg7);
            a = b = 1;
            break;
        case 'D':
            a = zone_table[zone].cmd[comm].arg1 = real_room(zone_table[zone].cmd[comm].arg1);
            break;
        }

        /*
         * If we ever received a negative value from any of the real_*
         * functions, we've got an invalid virtual number.  Thus we
         * disable the command with the special '*' zone command.
         */
        if (a < 0 || b < 0) {
            vmudlog(CMP, "Invalid virtual number in zone reset command: "
                         "zone #%d, command %d.  Command disabled.\n",
                zone_table[zone].number, comm + 1);

            zone_table[zone].cmd[comm].command = '*';
        }
    }
}
