# LS-1 Location-Read Allow-List

This ledger names the whole-file exemptions `tools/location_read_census.py --check` reads via
`--exceptions` (default path). Wave LS-1 routes every raw `->in_room` / `.in_room` / `world[` /
`next_in_room` **READ** inside the six source-bearing libraries (`entity`/`persist`/`world`/
`combat`/`pathfind`/`script`/`olc`) through the Stage-1 Placement API (`location_of`/`room_by_id`/
`room_by_id_total`/`occupants`, `src/handler.h`). The files below are the ONE place raw location
access legitimately remains after LS-1: they ARE the representation the Placement API wraps, not
call sites that should route through it. Every other legitimately-retained raw site (a write, an
`obj->in_room` object-location read, a flagged cursor/splice/peek idiom, or a resolver's own
backing-store body) is annotated in place with a trailing `// LS1-ALLOW: <reason>` comment instead
of a whole-file exemption — see `.superpowers/sdd/ls1-census.md` Step 8 for the full design and
`.superpowers/sdd/ls1-task-3-report.md` for the annotation inventory.

| Path | Reason |
| --- | --- |
| `src/entity/placement.cpp` | representation-owner — defines `location_of`/`set_location`/`is_in_room`/`occupants`/the `room_by_id`/`room_by_id_total`/`zone_by_id`/`obj_index_by_id` resolvers, and the char-to-room/room-to-char attach/detach mutation primitives themselves. |
| `src/entity/containment.cpp` | representation-owner — the obj↔room/char/obj containment core (`obj_to_room`/`obj_from_room`/etc.), the object-placement mirror of `placement.cpp`. |

`zone_table[` is explicitly **not** a token this gate tracks (LS-1 census Discrepancy 2 — out of
this program's charter; `zone_by_id()` exists but converting `zone_table[` call sites is not this
wave's exit criterion).

The eight accepted `LS1-ALLOW` reason prefixes (hardcoded in the script's
`ALLOWED_REASON_PREFIXES`; any other reason fails `--check` as `invalid-reason`): `save-next`,
`manual occupant-list splice`, `peek-ahead`, `manual first-match advance`,
`in_room used as mutable room cursor`, `write`, `obj-location`, `resolver-impl`.

**KNOWN BOUNDARY (census-sanctioned; LS-3's work — see the program spec's "The gate and the macro
boundary"):** the gate scans `.cpp` bodies only. Raw reads hiding behind `src/utils.h` macros
(`EXIT`/`OUTSIDE`/`IS_WATER`/`SUN_PENALTY` expand to `world[(ch)->in_room]` at their call sites —
roughly 90 sites across the converted libraries) are invisible to it: the call site carries no
literal token and headers are unscanned. A macro call site therefore reads as clean to this gate
while still touching the raw representation one level down; LS-3's representation swap converts
those macro bodies directly. Do not read this gate's green as "zero raw reads in the absolute" —
it is "zero raw *literal-token* reads in library `.cpp` bodies outside this ledger."
