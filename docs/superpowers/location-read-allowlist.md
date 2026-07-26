# LocationSystem Stage-1 Location-Read Allow-List

This ledger names the whole-file exemptions `tools/location_read_census.py --check` reads via
`--exceptions` (default path). `LS1-ALLOW` names **L**ocation**S**ystem Stage-**1** — Stage 1 spans
both wave LS-1 and wave LS-2, not "wave 1" specifically; Stage 2 (LS-3) deletes the annotated lines
outright when it swaps the representation, rather than re-marking them. The marker spelling is kept
verbatim across both waves deliberately (see `.superpowers/sdd/ls2-census-b.md` §3.6): renaming it
would churn 116+ already-annotated lines across 17 files plus this ledger, the script, `AGENTS.md`,
the program spec, and six task reports, for zero functional gain.

Wave LS-1 routed every raw `->in_room` / `.in_room` / `world[` / `next_in_room` **READ** inside the
six source-bearing libraries (`entity`/`persist`/`world`/`combat`/`pathfind`/`script`/`olc`) through
the Stage-1 Placement API (`location_of`/`room_by_id`/`room_by_id_total`/`occupants`,
`src/handler.h`). Wave LS-2 widened the census tree-wide: it is now a recursive sweep of production
`src/**` for `.cpp`/`.h`/`.hpp` — `src/app` and every header are in scope for the first time,
alongside the seven libraries LS-1 already covered — so the program's Stage-1 exit criterion ("raw
location representation access exists ONLY inside the allow-listed representation-owner set") is
mechanically true for all of production `src/`, not a hand-picked directory list — **for the four
tracked tokens** this gate scans (`->in_room` / `.in_room` / `world[` / `next_in_room`), not for
every conceivable form of raw representation access (O-I8,
`.superpowers/sdd/ls2-wholebranch-review-opus.md`). `src/tests` is one deliberate exception (see "The
`src/tests` deferral" below); three more are untracked by construction, since none is one of the four
tokens — `room_data::people`/`.people` direct occupant-chain access, `char_data::was_in_room` (a
second parallel location store), and the `&world`/`get_world()` singleton handoff (`db_boot.cpp`,
`src/singleton.h`). All three are named LS-3 inputs recorded in
`docs/superpowers/specs/2026-07-23-locationsystem-program-design.md`'s own As-built "out of LS-2's
charter" list, not oversights this ledger silently omitted.

The files below are the ONE place raw location access legitimately remains as a **whole-file**
exemption: they ARE the representation the Placement API wraps, not call sites that should route
through it. Every other legitimately-retained raw site (a write, an `obj->in_room` object-location
read, a flagged cursor/splice/peek idiom, a resolver's own backing-store body, a header declaration
or API body that IS the representation, or an `in_room`-named field that isn't a char location at
all) is annotated in place with a trailing `// LS1-ALLOW: <reason>` comment instead of a whole-file
exemption — see `.superpowers/sdd/ls1-census.md` Step 8 and `.superpowers/sdd/ls2-census.md` R9 for
the full design, `.superpowers/sdd/ls1-task-3-report.md` for the LS-1 annotation inventory, and
`.superpowers/sdd/ls2-task-{3a,3b,3c,3d}-report.md` for the LS-2 app-tier inventory.

| Path | Reason |
| --- | --- |
| `src/entity/placement.cpp` | representation-owner — defines `location_of`/`set_location`/`is_in_room`/`occupants`/the `room_by_id`/`room_by_id_total`/`zone_by_id`/`obj_index_by_id` resolvers, and the char-to-room/room-to-char attach/detach mutation primitives themselves. |
| `src/entity/containment.cpp` | representation-owner — the obj↔room/char/obj containment core (`obj_to_room`/`obj_from_room`/etc.), the object-placement mirror of `placement.cpp`. |

No production header or `src/app` file needed a new whole-file row when the scan widened: the four
production-header sites LS-2 found (below) each took a tight per-line annotation instead, and
`src/utils.h` needed nothing once its macro bodies converted (see "The `utils.h` macro boundary is
now closed" below).

`zone_table[` is explicitly **not** a token this gate tracks (LS-1 census Discrepancy 2 — out of
this program's charter; `zone_by_id()` exists but converting `zone_table[` call sites is not this
wave's exit criterion).

## The eleven accepted `LS1-ALLOW` reason prefixes

Hardcoded in the script's `ALLOWED_REASON_PREFIXES`; any other reason fails `--check` as
`invalid-reason` (self-tested — an off-list reason trips the gate even when a whole-file exemption
or a well-formed annotation is present elsewhere in the same file). LS-1 minted the first eight; LS-2
T5 added the last three (`.superpowers/sdd/ls2-census.md` R9):

- `save-next` — a save-next-then-advance idiom whose body relocates the current node.
- `manual occupant-list splice` — hand-rolled chain surgery outside the Placement API's own
  mutation primitives.
- `peek-ahead` — a lookahead read that doesn't drive the walk itself.
- `manual first-match advance` — a find-first idiom with its own early-exit shape.
- `in_room used as mutable room cursor` — Family D: `in_room` temporarily stashed with a
  **different kind of value** (a VNUM, not a location index) between two calls, never a genuine
  location read/write pair.
- `write` — any raw assignment into the representation; LS-2 is reads-only, so every write stays
  raw by design (LS-3 owns writes). On a `\`-continued macro line the annotation MUST be the block
  form `/* LS1-ALLOW: write ... */ \` placed **before** the backslash — a `//` there is a compile
  error, since translation phase 2 (line splicing) runs before phase 3 (comment stripping) and
  would swallow the following physical line. `src/interpre.h:90` (`ASSIGNROOM`'s write) is the one
  production site using this form.
- `obj-location` — `obj_data::in_room`, a genuine but separately-owned object-location field, not
  the char-location field this gate's main charter converts.
- `resolver-impl` — the Stage-1 resolvers' own backing-store bodies (`db_world.cpp`'s
  `room_by_id_impl`/`room_by_id_total_impl`, `room_data::operator[]`'s recursive fallback): the
  literal `world[]` access IS the resolver implementation, not a caller.
- `representation-decl` **(LS-2)** — a struct field declaration that IS the representation, never a
  call site and never convertible. One production site: `src/core/include/rots/core/character.h:858`
  (`char_data::next_in_room`'s own declaration).
- `representation-impl` **(LS-2)** — a Stage-1 Placement API body that legitimately walks the raw
  chain, living in a header rather than in an allow-listed `.cpp` owner. Two production sites:
  `src/handler.h`'s `occupant_range::iterator::operator++` and
  `const_occupant_range::iterator::operator++` bodies (moved verbatim from `placement.cpp` in LS-1
  Task 1b).
- `not-a-location` **(LS-2)** — an `in_room`-named field that isn't a character location at all.
  Five production sites, all `src/app/shop.cpp`: `shop_data::in_room` holds a **shop VNUM**, a
  fourth `in_room` field in this tree alongside `char_data::in_room` (the subject),
  `obj_data::in_room` (`obj-location`), and `char_data::was_in_room` (a second parallel location
  store, out of LS-2 scope, an LS-3 input — see `.superpowers/sdd/ls2-census.md` R5).

## The `src/tests` deferral

`src/tests` is excluded from the scan via the script's module-level `DEFERRED_DIRS = ("tests",)`
constant — **deliberately not a ledger row**. A ledger row asserts a file is a permanent
representation owner; the test tier is not one, it is a wave that has not happened yet.
`.superpowers/sdd/ls2-census.md` R2 found the tier is **95% a WRITES problem** (fixture construction
— char-location writes, occupant-chain splices, `world[]` assignment targets), not the READS problem
this wave's charter covers, and the right fix is a two-entry-point test helper
(`test_set_location`/`ScopedRoomOccupants`) that wave **LS-3a** owns alongside the rest of the
program's write conversion. Every `--check` run prints a one-line notice — `[deferred] N file(s)
under src/tests unscanned this wave (LS-2 R2/A-2 -- retired by wave LS-3a).` — so the deferral stays
visible rather than silent; `N` is independently reproducible via `find src/tests -type f \( -name
'*.cpp' -o -name '*.h' -o -name '*.hpp' \) | wc -l`. LS-3a's own T0 re-derives the test-tier numbers
from scratch (`.superpowers/sdd/ls2-census.md` A-6 — the LS-2 census's own B/C sub-agents produced
disagreeing test-tier headline figures, both explicitly not inherited).

## The `utils.h` macro boundary is now CLOSED

Wave LS-1's original boundary note (superseded, kept here as history) read: *"the gate scans `.cpp`
bodies only — raw reads hiding behind `src/utils.h` macros (`EXIT`/`OUTSIDE`/`IS_WATER`/
`SUN_PENALTY`, ~90 sites) are invisible to it."* Wave LS-2 Task T2 closed this boundary directly,
**not by widening the gate around it**: it converted all seven `world[]`-referencing macro bodies in
`src/utils.h` (`IS_DARK`, `IS_SUNLIT_EXIT`, `IS_SHADOWY_EXIT`, `SUN_PENALTY`, `OUTSIDE`, `EXIT`,
`IS_WATER` — `IS_LIGHT`/`IS_SUNLIT` needed no edit, they delegate to `IS_DARK` without referencing
`world` directly) to call `room_by_id_total`/`room_of` instead of indexing `world[]` literally, so
their **282** tree-wide call sites now inherit Stage-1 routing automatically, with zero calling code
changed. Combined with Task T5's header sweep, `src/utils.h` itself now carries **zero** raw
location tokens and needs no ledger entry or annotation of its own. Do not read a future clean
`--check` as evidence this class of indirection can never recur — it is exactly what the header
sweep (§ above) now stands watch over: any future edit that reintroduces a raw `world[]`/`->in_room`
into a macro body, or any other header, trips the gate the same way a `.cpp` body would.
