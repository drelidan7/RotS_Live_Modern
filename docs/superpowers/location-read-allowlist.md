# LocationSystem Stage-1 Location-Read Allow-List

This ledger names the whole-file exemptions `tools/location_read_census.py --check` reads via
`--exceptions` (default path). `LS1-ALLOW` names **L**ocation**S**ystem Stage-**1** — Stage 1 spans
waves LS-1, LS-2 and LS-3a, not "wave 1" specifically; Stage 2 (LS-3b) deletes the annotated lines
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
mechanically true for all of production `src/`, not a hand-picked directory list.

Wave **LS-3a T4** closed the last two structural gaps in that claim, and the sweep is now
**whole-tree and five-token**:

- `->people` / `.people` — the occupant chain's HEAD, the other half of the same intrusive list
  `next_in_room` walks — became the **fifth tracked token** (ruling R-B6). Until then a green gate
  was misleading. Measured at the commit that added it: **56** production lines and **68**
  test-tier lines carried the token, and **29** production lines were neither annotated nor inside
  an allow-listed owner — among them `src/handler.h`'s own `occupant_range`/`const_occupant_range`
  constructors, whose `operator++` siblings had been annotated since LS-2 (ruling R-C7). Twenty-four
  of the 29 were chain-HEAD reads and converted outright to the Stage-1 `first_occupant()` accessor
  that LS-3a T1 had landed consumer-free for exactly this purpose (ruling R-C6's own named
  `db_world.cpp` act()-anchor set among them); the other five are the two header API bodies above
  and three writes.
- The `src/tests` deferral is **retired** (ruling R-B8; see "The `src/tests` deferral is RETIRED"
  below). `MINIMUM_SCANNED_FILE_COUNT` rose 100 → 250 in the same commit, because the scanned count
  went 181 → 307 and a floor left at 100 would have kept passing even if the entire test tier
  silently dropped back out of the sweep.

**Wave LS-3b T5** (THE SPLIT) privatized `char_data::in_room` / `char_data::next_in_room` /
`room_data::people` **by rename**, to `char_data::ls_location_id_` / `char_data::ls_next_in_room_` /
`room_data::ls_first_occupant_` (ruling R-3b-A — keying is the private handle, not an external
pointer-keyed map; see `.superpowers/sdd/ls3b-global-constraints.md`'s T0 close-out). The old
spellings genuinely no longer exist on either struct — the rename is what makes corrected
criterion (a)'s "no location field" clause provable at all, and LS-3b's own compile-time absence
assertion (`src/entity/placement.cpp`, added by **T8**, see below) is its mechanical witness.

**Wave LS-3b T8** re-tokened this script for the post-split tree, and the sweep is now
**whole-tree and EIGHT-token**:

- The original five tokens (`->in_room`/`.in_room`/`::in_room`/`world[`/`next_in_room`/`people`)
  **all stay, unchanged, meaning narrowed** rather than retired. `obj_data::in_room` and
  `shop_data::in_room` were never touched by a chars-only wave, so `in_room`/`.in_room`/`::in_room`
  keep policing that population exactly as before; `world[` keeps policing corrected criterion
  (b)'s "`room_data::operator[]` preserved byte-for-byte" clause, untouched by the swap. `people` and
  `next_in_room` no longer match any live `char_data`/`room_data` site (there is nothing left to
  match — the fields are gone), so every surviving annotation under them is now a **reintroduction
  tripwire**: if a future edit ever restores the public spelling, the token fires again immediately,
  before the compiler even gets a chance to. Retiring either was considered and rejected — see
  `ls3b-census-d.md` §6.1's own "retire NO token" recommendation, which T8 adopted verbatim.
- **Three new BARE-WORD tokens** — `ls_location_id_` / `ls_next_in_room_` / `ls_first_occupant_` —
  catch the private spellings themselves. Unlike `.in_room`/`.people` (accessor-anchored, since
  `in_room`/`people` are common enough words to need one), these three identifiers are unique
  tree-wide, so each pattern is a bare `\bIDENTIFIER\b` with no accessor requirement — the same
  design `next_in_room` already used, and for the same reason: a bare-word pattern also catches the
  field's own DECLARATION (there is no `->`/`.`/`::` before a declaration), which an
  accessor-anchored pattern structurally cannot see. Measured at T8 kickoff: every production call
  site the T5 rename touched already carried its ORIGINAL annotation comment (the rename changed
  only the field spelling, not the prose beside it) — so adding the three tokens found the annotation
  contract already satisfied everywhere **except** the two fields' own declarations
  (`character.h:861`'s `ls_location_id_`, `room.h:126`'s `ls_first_occupant_`), which had never
  needed one before this task and gained a `representation-decl` annotation apiece.
- One reason prefix retired in the same task: `manual occupant-list splice` (zero production lines
  since at least the T0 census; its six test-tier sites sit inside the whole-file-exempt
  `src/tests/test_placement.h` and were never gate-enforced either way — see "The nine accepted
  `LS1-ALLOW` reason prefixes" below). `MINIMUM_SCANNED_FILE_COUNT` rose 250 → 300 against a
  315-file scan.

That still is not every conceivable form of raw representation access (O-I8,
`.superpowers/sdd/ls2-wholebranch-review-opus.md`). **One** named exclusion remains untracked by
construction, since it is not one of the eight tokens: `char_data::was_in_room`, a second parallel
location store. It is a named LS-3b input recorded in
`docs/superpowers/specs/2026-07-23-locationsystem-program-design.md`'s own As-built "out of LS-2's
charter" list, not an oversight this ledger silently omits. Two entries LS-2 listed beside it no
longer apply: `.people` is now tracked, and **`get_world()` was DELETED outright** by LS-3a T2
tranche 2d as a zero-caller dead-code rider (ruling R-C8) — only the `&world` array-address handoff
in `db_boot.cpp`/`src/singleton.h` survives it, and that hands the room table over as a whole rather
than reading any character's location.

The files below are the ONE place raw location access legitimately remains as a **whole-file**
exemption: they ARE the representation the Placement API wraps, not call sites that should route
through it. Every other legitimately-retained raw site (a write, an `obj->in_room` object-location
read, a flagged cursor/splice/peek idiom, a resolver's own backing-store body, a header declaration
or API body that IS the representation, or an `in_room`-named field that isn't a char location at
all) is annotated in place with a trailing `// LS1-ALLOW: <reason>` comment instead of a whole-file
exemption — see `.superpowers/sdd/ls1-census.md` Step 8 and `.superpowers/sdd/ls2-census.md` R9 for
the full design, `.superpowers/sdd/ls1-task-3-report.md` for the LS-1 annotation inventory, and
`.superpowers/sdd/ls2-task-{3a,3b,3c,3d}-report.md` for the LS-2 app-tier inventory.

<!-- LOCATION-READ-ALLOWLIST-TABLE -->
| Path | Reason |
| --- | --- |
| `src/entity/placement.cpp` | representation-owner — defines `location_of`/`set_location`/`is_in_room`/`occupants`/the `room_by_id`/`room_by_id_total`/`zone_by_id`/`obj_index_by_id` resolvers, and the char-to-room/room-to-char attach/detach mutation primitives themselves. |
| `src/entity/containment.cpp` | representation-owner — the obj↔room/char/obj containment core (`obj_to_room`/`obj_from_room`/etc.), the object-placement mirror of `placement.cpp`. |
| `src/tests/test_placement.h` | representation-owner (test tier, LS-3a) — `ScopedRoomOccupants`/`ScopedZoneTableOwner`, the header the whole test tier's occupant-chain construction collapses into. Every raw touch in it *also* carries its own per-line annotation, so it reads correctly under either regime. |
| `src/tests/placement_tests.cpp` | representation-owner (test tier, LS-3a) — the test **for** the representation owner: it pins `set_location`/`is_in_room`/`occupants`/`first_occupant`/the attach-detach primitives against the raw field and the raw chain, on purpose, so a writer defect cannot be masked by a matching reader defect. |
| `src/tests/test_world.h` | representation-owner (test tier, LS-3a) — `ScopedTestWorld` **is** the test tier's `world[]`: it allocates, resets and tears down the room table every fixture resolves through. |

No production header or `src/app` file needed a new whole-file row when the scan widened: the four
production-header sites LS-2 found (below) each took a tight per-line annotation instead, and
`src/utils.h` needed nothing once its macro bodies converted (see "The `utils.h` macro boundary is
now closed" below). The three test-tier rows above are the **only** rows LS-3a T4 added when
`src/tests` joined the scan — census B's predicted "owner trio" exactly (ruling R-B1), and every
other test file in the tier came in clean or took per-line annotations. `test_placement.h` would
have passed without its row (its eight raw lines are all annotated); it is listed anyway because
the header's own banner comment declares it the intended home for the tier's raw lines, and a
reader must not have to reverse-engineer that from the absence of a row.

`zone_table[` is explicitly **not** a token this gate tracks (LS-1 census Discrepancy 2 — out of
this program's charter; `zone_by_id()` exists but converting `zone_table[` call sites is not this
wave's exit criterion).

## The nine accepted `LS1-ALLOW` reason prefixes

Hardcoded in the script's `ALLOWED_REASON_PREFIXES`; any other reason fails `--check` as
`invalid-reason` (self-tested — an off-list reason trips the gate even when a whole-file exemption
or a well-formed annotation is present elsewhere in the same file). LS-1 minted the first eight; LS-2
T5 added three more (`.superpowers/sdd/ls2-census.md` R9), for eleven. **LS-3a T4 minted none** —
ruling R-B7 held the count at eleven when `src/tests` and the `.people` token joined the scan, and
the pre-reserved `test-fixture` prefix was deliberately NOT created; every surviving test-tier site
fit an existing prefix. **LS-3b T2 RETIRED `in_room used as mutable room cursor`** (the fail-closed
burndown rule adopted by `.superpowers/sdd/ls3b-global-constraints.md`): its last 28 production
lines converted onto `rots::entity::ScopedRenderLocation` (ruling R-3b-B) in the same commit that
removed the prefix from `ALLOWED_REASON_PREFIXES`, bringing the count to ten. Historical note, kept
for readers of earlier wave records: that retired prefix covered Family D — `in_room` temporarily
stashed with a **different kind of value** (a VNUM, not a location index) between two calls, never a
genuine location read/write pair. **LS-3b T8 RETIRED a second prefix, `manual occupant-list
splice`**, bringing the count to **nine**: the fail-closed burndown audit (re-measuring every
remaining prefix's live line count tree-wide, per `ls3b-t8-report.md`) found it had carried **zero**
production lines since at least the T0 census (`ls3b-census-d.md` §6.2: "0 production / 6 test"),
and its six surviving test-tier sites (`src/tests/test_placement.h:354-365`) sit inside a file this
gate already exempts **whole** — `findings_for_file` returns before ever reaching the annotation
check there, so those six comments were inert documentation, not gate-enforced text, both before and
after the retirement. Removing a prefix is fail-closed by construction (an off-list reason still
fails `--check` as `invalid-reason`, self-tested by the `retired-prefix-no-longer-authorized` case),
so this retirement changed nothing observable anywhere in the tree.

- `save-next` — a save-next-then-advance idiom whose body relocates the current node.
- `peek-ahead` — a lookahead read that doesn't drive the walk itself.
- `manual first-match advance` — a find-first idiom with its own early-exit shape.
- `write` — any raw assignment into the representation. LS-2 was reads-only, so every write stayed
  raw by design; LS-3a routes the *convertible* ones through `set_location()`, and this prefix now
  marks what legitimately remains — fresh-room initialization (`src/world/db_world.cpp:702`/`:1889`)
  and OLC editor-scratch rooms (`src/olc/shaperom.cpp:157`/`:1284`). On a `\`-continued macro line
  the annotation MUST be the block form `/* LS1-ALLOW: write ... */ \` placed **before** the
  backslash — a `//` there is a compile error, since translation phase 2 (line splicing) runs
  before phase 3 (comment stripping) and would swallow the following physical line.
  `src/interpre.h:90` (`ASSIGNROOM`'s write) is the one production site using this form. A second
  multi-line shape matters as of LS-3a T4: clang-format split `shaperom.cpp:157`/`:1284` so the
  token sits on one line and its `= 0;` on the next. The gate is deliberately **line-based** and
  does no read-vs-write classification of its own (ruling AM-5 withdrew a proposed multi-line
  matcher), so the annotation goes on the **token's** line and the reason — not the script — is
  what records that it is a write.
- `obj-location` — `obj_data::in_room`, a genuine but separately-owned object-location field, not
  the char-location field this gate's main charter converts.
- `resolver-impl` — the Stage-1 resolvers' own backing-store bodies (`db_world.cpp`'s
  `room_by_id_impl`/`room_by_id_total_impl`, `room_data::operator[]`'s recursive fallback): the
  literal `world[]` access IS the resolver implementation, not a caller.
- `representation-decl` **(LS-2)** — a struct field declaration that IS the representation, never a
  call site and never convertible. One production site: `src/core/include/rots/core/character.h:858`
  (`char_data::next_in_room`'s own declaration).
- `representation-impl` **(LS-2)** — a site that IS the representation rather than a call site that
  should route through the API. Three classes now use it:
  1. A Stage-1 Placement API body that legitimately touches the raw chain while living in a header
     rather than in an allow-listed `.cpp` owner — `src/handler.h`'s
     `occupant_range::iterator::operator++` and `const_occupant_range::iterator::operator++` bodies
     (moved verbatim from `placement.cpp` in LS-1 Task 1b), the two `first_occupant()` overloads
     (LS-3a T1), and, added by **LS-3a T4**, both range constructors' own
     `first_(room ? room->people : nullptr)` chain-head snapshots — invisible to the gate until
     `.people` became a token, and the last unannotated lines in a class whose `operator++` siblings
     were annotated from the start (ruling R-C7).
  2. A test-tier fixture helper publishing or restoring a chain the Placement API cannot express
     — `src/tests/test_placement.h`'s `ScopedRoomOccupants` (LS-3a T1; it deliberately does not
     call `char_to_room()`, ruling R-B3).
  3. **(LS-3a T4)** An occupant-chain **SHAPE assertion**: a test that pins the raw `next_in_room`
     links themselves. Fourteen sites — eight in `src/tests/spec_pro_tests.cpp` (the ferry
     merge-order contract) and six in `src/tests/load_room_placement_tests.cpp` (owner/follower
     ordering). These are the one test-tier class that could not be converted: no Stage-1 API
     expresses a *tail* walk (`first_occupant()` closed the six head reads beside them, and
     `occupants()` expresses iteration, not link identity), and the chain shape is precisely the
     property LS-3b rewrites — so they must stay raw, stay visible, and be found by a reader
     grepping the annotation. Ruling R-B7 forbade minting a prefix for them; of the eleven,
     `representation-impl` is the only honest fit, since the assertion's subject IS the
     representation. (`peek-ahead` was considered and rejected: it names a *walk* idiom's
     lookahead, and these are not walks.)
- `not-a-location` **(LS-2)** — an `in_room`-named field that isn't a character location at all.
  Note what this does **not** cover: `src/olc/shaperom.cpp:157`/`:1284` write `->room->people` on an
  editor-scratch `room_data`, and LS-3a T4 annotated them `write`, not `not-a-location` — the room
  is scratch, but the field genuinely IS the occupant-chain head, and LS-3b must find every
  representation write. Five production sites, all `src/app/shop.cpp`: `shop_data::in_room` holds a **shop VNUM**, a
  fourth `in_room` field in this tree alongside `char_data::in_room` (the subject),
  `obj_data::in_room` (`obj-location`), and `char_data::was_in_room` (a second parallel location
  store, out of LS-2 scope, an LS-3 input — see `.superpowers/sdd/ls2-census.md` R5).

## The `src/tests` deferral is RETIRED

LS-2 excluded `src/tests` from the scan via the script's module-level `DEFERRED_DIRS = ("tests",)`
constant — **deliberately not a ledger row**, because a ledger row asserts a file is a permanent
representation owner and the test tier was not one, it was a wave that had not happened yet.
`.superpowers/sdd/ls2-census.md` R2 found the tier is **95% a WRITES problem** (fixture construction
— char-location writes, occupant-chain splices, `world[]` assignment targets), not the READS problem
LS-2's charter covered.

**Wave LS-3a is that wave, and T4 retired the deferral** (ruling R-B8). T1 built the fixture helper
the deferral was granted for (`src/tests/test_placement.h`'s `ScopedRoomOccupants`, plus
`ScopedZoneTableOwner`) and T3 migrated the whole tier onto it, collapsing ~800 raw representation
sites across 36 files down to the three owner files rowed in the table above plus fourteen annotated
chain-shape assertions. `DEFERRED_DIRS`, `is_under_deferred_dir()` and the `[deferred] N file(s)`
notice are **gone from the script entirely** — not emptied but deleted, so no future wave can
re-introduce a blind spot by adding a name to a tuple. `--check` now prints `[scanned] N file(s)`
instead, and the scanned count went **181 → 307**; `MINIMUM_SCANNED_FILE_COUNT` rose 100 → 250 in
the same commit so the floor still fails closed against a broken scan path. The gate's own
`--self-test` carries the inverse of LS-2's probe: where it once asserted a `src/tests` file was
deferred and its notice printed, it now asserts that an unannotated write in a `src/tests` file is
**flagged**, and that no `[deferred]` notice is printed at all.

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

## The compile-time absence assertion — corrected criterion (a)'s mechanical witness

This gate can only see PRESENCE: every one of its eight tokens flags a raw spelling wherever one
exists. It structurally cannot see an ABSENCE, so a clean `--check` alone never proved that
`char_data::in_room` / `char_data::next_in_room` / `room_data::people` are actually gone rather than
merely unused. **LS-3b T8** closed that gap with a compile-time check, not a gate line:
`src/entity/placement.cpp` defines three C++20 concepts (`HasPublicInRoomMember<T>`,
`HasPublicNextInRoomMember<T>`, `HasPublicPeopleMember<T>`, each a `requires(T instance) { ... }`
expression naming the retired member) and three `static_assert`s that fail the **build** if any
concept is ever satisfied for `char_data`/`room_data` — i.e. if the old public spelling is ever
reintroduced. A `requires`-expression is SFINAE-friendly: when the named member does not exist the
expression is ill-formed and the concept is simply `false`, never a hard error, which is exactly the
"quiet while absent, loud the instant it comes back" behavior the witness needs.

It lives in `placement.cpp` rather than a standalone test executable for two reasons: that file is
already a whole-file `LS1-ALLOW` exemption, so the literal `.in_room`/`.next_in_room`/`.people`
probe spellings inside the `requires`-expressions need no per-line annotation of their own; and
`rots_entity` is an always-built target on every preset (host, container, MSVC, sanitizer), so the
witness can never be silently skipped by an optional test target going unbuilt. It adds **no** new
ctest entry — a reintroduction is a compile failure, caught by every build gate this repository
already runs, not a new named check to remember to run. See `.superpowers/sdd/ls3b-t8-report.md` for
the sabotage proof (a decoy member added for each of the three fields in turn, each reddening its own
`static_assert` with a named, readable error, then restored).
