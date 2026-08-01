# The room-resolve retirement program ("RR") — design

**Date:** 2026-07-31 (amended same day after dual adversarial spec review — see §9)
**Branch (Wave R1):** `arch/rr1-census` (off master @`a70407a7`, 1860 tests)
**Status:** program design + Wave R1 spec. Waves R2+ get per-wave briefs off this document
(the LocationSystem precedent: one program design doc, per-wave operative specs).
**Owner ruling (2026-07-31):** end-state failure semantics = **proof-driven burndown to
abort** — no behavior change at any site before its proof or guard lands; the fallback arms
flip to tripwire aborts only when the burndown reaches zero.
**Open owner ruling required before R-final (named here so it cannot be improvised): RR-O-1**
— see §2a. The flip is blocked on it.

## 1. The defect class

`room_data::operator[]` (`src/world/db_world.cpp:2065`) is a *total* function over invalid
input, by these degrade paths:

| Input | Behavior today |
|---|---|
| `i < 0` | `mudlog("world[] called for negative room number.", NRM, LEVEL_GOD, TRUE)` + **return `world[0]`** |
| `i >= BASE_LENGTH`, beyond all extensions | mudlog (LEVEL_GRGOD) + **return `world[r_immort_start_room]`** |
| same, but `i == r_immort_start_room` | mudlog (LEVEL_GRGOD, fires first) + **`exit(0)`** — a recursion guard on the fallback itself, **unreachable by construction today**: `r_immort_start_room` is boot-clamped in-range (`db_world.cpp:825-829`), so `i >= BASE_LENGTH && i == r_immort_start_room` cannot hold. Not a co-equal burndown surface; it is flipped with its parent arm for free (review O-11) |
| **`i` in the allocated-but-uncreated window** — `create_bulk` allocates `amount + EXTENSION_SIZE` rooms and dummy-initializes the trailing block (`db_world.cpp:2041-2051`), so indices in `(top_of_world, BASE_LENGTH)` whose slot was never `create_room`ed resolve to a **dummy room, silently — no mudlog, no fallback, no arm** (review O-3). Same for allocated-but-never-created extension slots. This is the *fourth degrade path* and the commonest corrupt-range silent read |
| `BASE_WORLD == nullptr` | `abort()` (already a tripwire — LS-3a hardened it; not part of this campaign) |

`room_by_id_total()` deliberately preserves the graceful contract ("graceful total
function", `db_world.cpp:292-300`), and `room_of(ch)` = `room_by_id_total(location_of(ch))`
(`entity/placement.cpp:354`). Consequence: every unguarded resolver-reaching site silently
reads room 0, the immortal start room, or a dummy room when handed an invalid id — the
corruption surface m-14 exposed. The LS-3b deferred-MINORs follow-up retired the one known
*producer* (the bugged login arm); this campaign retires the *consumer-side tolerance*.

**The resolver-reaching surface** (all spellings; both reviews attacked completeness — O-1/
F-1/F-2/F-3 — and this list is the corrected result):

1. `room_of(` — ~297 raw production lines; `room_by_id_total(` — ~252; `EXIT(` — ~221.
   (Basis: word-boundary `grep -c` line counts at `a70407a7`, `src/tests` excluded,
   comments included — RAW figures, labeled as such (O-12/F-9). The R1 census defines the
   authoritative masked basis and re-measures; `MAXIMUM_TODO_COUNT` is seeded from the
   census's own numbers, never from these.)
2. **`world[` / `::world[` — raw indexing invokes `operator[]` directly.** ~28-31 live
   production sites survive LS-1/2/3 under `LS1-ALLOW` annotations — including
   `act_obj2.cpp:726`/`:769` (the m-14 light-bump lines this program cites as motivation),
   `mudlle.cpp:450`/`:467`, `handler.cpp:539`, `mage.cpp:1708-1715`, and `db_world.cpp`'s
   loader writes. **An `LS1-ALLOW` annotation is NOT a proof**: the location gate licenses
   *representation access*; this gate asks *input validity* — different questions,
   different ledgers (O-1/F-1, both reviews' top finding).
3. **Hook-dispatch aliases**: `world_room_vnum(` (`db_world.cpp:2116`, calls
   `room_by_id_total`) and its consumer spelling `rots::persist::dispatch_room_vnum(` —
   the only alias path at HEAD (verified: no `&room_by_id_total`/`&room_of` references
   exist), pinned as tokens now precisely because it is currently unique (F-2).
4. **The resolver-expanding macro family**, re-derived by R1 from the `#define` bodies of
   **all scanned headers** — not `utils.h` alone (F-3): at HEAD, `EXIT` (:721), `OUTSIDE`
   (:719), `SUN_PENALTY` (:517) over `room_of`; `IS_DARK` (:328), `IS_LIGHT` (:332, via
   `IS_DARK`), `IS_SUNLIT` (:334), `IS_SUNLIT_EXIT` (:511), `IS_SHADOWY_EXIT` (:514),
   `IS_WATER` (:796) over `room_by_id_total`; **plus `ASSIGNROOM` (`interpre.h:86-90`)**,
   which reaches `operator[]` through `world[real_room(room)]` (4 call sites; internally
   guarded, so it classifies as one PROVEN row — but it must be *enumerated*). A macro call
   site is a resolver site even though no resolver token appears on the line.

## 2. End-state semantics (the ruling, made precise)

**Validity, defined** (O-3): a room id `i` is valid iff its slot is allocated AND created —
`world[i].number >= 0`. `top_of_world` alone is the WRONG bound: legitimately created
runtime rooms (mage "crack" rooms, extension rooms) live above it — the tree already knows
this (`protocol.cpp:2946-2951` refuses a `top_of_world` guard for exactly this reason).

When the burndown reaches zero `TODO` rows, **R-final** flips the out-of-allocation arms
(negative; beyond-extension; the `exit(0)` recursion guard rides with its parent) to
**tripwire aborts** (`fprintf(stderr, ...)` + `abort()`, the existing `BASE_WORLD` arm's
idiom) and rewrites `room_by_id_total`'s contract comment. **The dummy-window path**
(`number < 0` reads inside the allocation) is IN the program's defect class; whether its
tripwire lands in the same flip or a sequenced follow-up inside R-final is an explicit
R-final decision input, resolved by a mini-census of the known fixture dependents
(`damage_test_context.h:25`, `mage_tests.cpp:48-62`, and whatever that census adds) —
not silently, either way (O-3).

`room_by_id`'s nullptr contract is untouched throughout. **Note its bound is
`rnum >= top_of_world` (`db_world.cpp:284`) — wrong for extension rooms** (O-4): a
`room_by_id` rewrite of a guarded site would turn a valid extension room into "absent".
Therefore: **GUARDED conversions retain the original token and add a guard around it; the
rewrite-onto-`room_by_id` shape is a program non-goal** until `room_by_id`'s bound is
itself fixed (recorded in §6). This also dissolves the stale-row contradiction both
reviews found (F-5/O-4): a guarded site still scans, so its row still matches.

Until the flip, **zero behavior change** except: `GUARDED` sites (each red-first-tested),
and the §2a replacement (its own ruling).

**No death tests** (standing rule): the abort arms are not unit-tested by dying. Their
unreachability is the census's claim — but not ONLY textual proof (F-6/O-9, both reviews):

- **The measured-zero gate (mandatory R-final precondition):** the fallback arms already
  self-instrument via two globally unique log strings (`db_world.cpp:2083`/`:2098`). After
  the §2a replacement lands (which retires the one *deliberate* producer of the negative
  resolve), every wave's full battery — ctest, monolithic, six-seed, both boot goldens,
  `make smoke-account`, the i386 battery, and R-final's extended soak (boot + scripted
  command sweep, defined in R-final's brief) — must show **zero occurrences** of either
  string in every produced server/test log. An **instrumentation pass in R-final** extends
  this to the currently-silent dummy-window path (a counted, rate-limited log) so the flip
  decision rests on *measured zero across the whole evidence chain*, not proofs alone.
- A runtime/compile-time kill switch for the flip (crash → config rollback on a live MUD)
  is a named R-final decision input for the owner (O-9); it trades against the repo's
  single-mode determinism culture and is not pre-decided here.

## 2a. The F17 collision — ruling RR-O-1 (required, owner-level)

`char_to_room()` resolves its argument **unconditionally, including NOWHERE**, under a
binding LS-3b ruling (F17; `placement.cpp:553-563`: "THE RESOLVER CALL IS PRESERVED
UNCONDITIONALLY … neither [side effect] may be short-circuited"). Production callers pass
NOWHERE by design (the O-5 supported shape: `objsave.cpp:511`, `limits.cpp:599`,
`comm.cpp:1306`), so **the negative-arm mudlog is deliberately reached today** — the
original §5 claim that it is "unreachable" was false (O-2, review-1's second BLOCKER), and
the flip as previously written would abort a supported production path and five checked-in
tests (`placement_tests.cpp:405/437/468/500`, `load_room_placement_tests.cpp:2711`).

**RR-O-1 (to be put to the owner before R-final; the program proceeds through R2+ without
it, but the flip is blocked on it):** overturn F17's unconditional-resolve clause for
`char_to_room` with this replacement, preserving each load-bearing side effect explicitly —

- the resolve becomes conditional (`room != NOWHERE`);
- the `BASE_WORLD`-unallocated tripwire is preserved *unconditionally* by an explicit
  check at the top of `char_to_room` (fixtures with no `ScopedTestWorld` must keep
  failing loudly — F17's second rationale, kept);
- the per-NOWHERE-placement god-channel mudlog is **deliberately retired** (it is noise
  about a supported shape, and it is exactly what blocks the measured-zero gate) — an
  operator-visible change the ruling must name;
- the five NOWHERE-shape tests keep passing (the resolve is skipped, no abort; their
  assertions are about linkage, which is unchanged).

## 3. Program architecture — classify in place, don't rename

The burndown unit is a **proof obligation**, not a call rewrite. Classes:

- **`PROVEN`** — the input cannot be invalid, with the proof stated in the row. Proof
  **kinds are a closed, gate-checked vocabulary** with the location census's
  prefix-boundary enforcement (O-6): `entry-guard`, `caller-contract`, `occupant-chain`,
  `loop-bound`, `dominating-resolve`. Every `PROVEN` row carries non-empty free text
  (gate-checked minimum length), and `entry-guard`/`dominating-resolve`/`caller-contract`
  rows carry a **mandatory `file:line` citation** the reviews can check mechanically
  (F-4). The model example, verified at HEAD: `db_players.cpp:1952`'s dispatch is PROVEN
  by the literal entry guard at `:1951` (`location_of(ch) != NOWHERE`). (The original
  draft's flagship example — "the command interpreter rejects roomless actors" — was
  **false**: `command_interpreter` checks position, not placement; the NOWHERE rejection
  lives only in `special()` (`interpre.cpp:1258-1264`). Caller-contract proofs are the
  hardest kind and always cite their guard site.) The ledger's proof-pattern prose also
  records the `occupant-chain` caveats (F-11/O-14): the O-5 invariant's contrapositive
  gives `!= NOWHERE` only; in-range follows from placement's M-1 precondition
  (`placement.cpp:369-395`) and appends-only allocation; the field==chain second half
  does NOT hold inside a `ScopedRenderLocation` window.
- **`GUARDED`** — guard added, **token retained** (§2). Row cites the wave and the
  red-first test.
- **`TEST-FIXTURE`** — `src/tests/` sites, enumerated but proof-free. Legal only under
  `src/tests/`, checked on the **resolved** path (the PR-#23 symlink lesson, named as a
  self-test direction — F-14).
- **`RESOLVER-IMPL`** — the implementation's own accesses, pinned by
  **file + function + count** (O-8): `placement.cpp`'s wrapper bodies, `db_world.cpp`'s
  `_impl`s and `operator[]`'s own recursion (raw `world[` lines — tokenized under this
  program per §1.2, which is exactly why the class must pin them), the world loader's
  construction-time writes. **`world_room_vnum` is NOT resolver-impl** — it is an ordinary
  consumer (PROVEN via `db_players.cpp:1951`, above) (O-8).
- **`DECL`** — declaration lines and macro-definition bodies that carry tokens without
  being call sites (`utils.h:61-62`, `handler.h:78/:120`, the nine macro bodies) — the
  location census's `representation-decl` analog (F-8). Closed set, gate-pinned.
- **`TODO`** — not yet classified. The ratchet class.

**Row schema and the anti-inheritance rule (O-5):** row key = file + function (or macro
name for `DECL`) + token, **plus a site count**; the gate requires, per key,
`scanned occurrences == Σ row counts` and rejects any key that doesn't sum. Adding a new
resolver site to a PROVEN function therefore **fails the gate** (count mismatch) instead
of silently inheriting the proof — the two-edit fix (code + row) is the point. One
function MAY hold rows of different classes for the same token (partial classification is
expressible). Two-room-argument macros (`IS_SUNLIT_EXIT`/`IS_SHADOWY_EXIT`) require the
row's proof text to cover **each** room-id argument (O-13).

**Two structural rules inherited from the LS program:** (1) marker-anchored fail-closed
ledger parsing — the marker appears exactly once *outside any fenced code block* (the M10
requirement, restated here so R1 implements it rather than the citation); proof text is
pipe-escaped; anchored tables, row floors, no directory exclusions, self-test sabotage
directions, and the consistency check enforced under BOTH census invocation shapes (the
W-1 path-equality design) from day one. (2) The gate is line-based and does no
reachability analysis; the ledger carries proofs, reviews audit them. A `PROVEN` row with
semantically wrong text is a review finding, not a gate finding — but §3's vocabulary,
length, and citation checks close the *trivially* gameable one-word-row class (O-6).

## 4. The census tool and gate (Wave R1's deliverable)

**`tools/room_resolve_census.py`** — a sibling of `location_read_census.py` (shared
masking by extraction into a helper or by copy — R1 decides; a shared module must not
weaken either tool's self-test).

- **Tokens:** `room_of(`, `room_by_id_total(`, `world[`/`::world[`, `world_room_vnum(`,
  `dispatch_room_vnum(`, and the macro family (§1.4), re-derived from ALL scanned headers.
  **Closed-world hardening (F-3):** a standing gate direction fails when any `#define`
  body in a scanned header contains a resolver token (including `world[`) whose macro name
  is not in the derived family — the m-13 lesson applied to macros.
- **Function-key derivation (O-7/F-7 — the tool's hardest problem, specified not
  improvised):** a brace-depth scanner over masked text; function headers recognized as
  either ordinary definitions or the macro definers (`ACMD(`, `SPECIAL(`, and the definer
  set R1 enumerates from `interpre.h` — 635 production functions use these); namespace
  blocks tracked for qualification. **Fail-closed:** a site whose enclosing function
  cannot be determined is a gate ERROR, not an "unknown" bucket. Dedicated self-test
  sabotage directions for: the `ACMD`-header case, a multi-line call, a macro-body site
  (keyed to the macro, class `DECL`), and an undeterminable-function probe (must fail).
- **Ledger:** `docs/superpowers/room-resolve-ledger.md`, one marker-anchored table
  (`<!-- ROOM-RESOLVE-CLASSIFICATION -->`), rows per §3's schema. Reconciliation both
  directions: unclassified site → fail; row matching nothing → fail; per-key count
  mismatch → fail (O-5).
- **The ratchet:** `--check` fails if the `TODO` count exceeds the checked-in
  `MAXIMUM_TODO_COUNT` literal; each drain wave lowers the literal in the same commit
  (the `MINIMUM_SCANNED_FILE_COUNT` pinned-literal practice — and, mirroring it, the
  **self-test pins the ceiling** so an accidental raise fails `--self-test`, not just
  review (F-10)). **Stated as the design's strongest property (O-10): a NEW resolver site
  anywhere in production is a new `TODO` row, which exceeds the ceiling — so unclassified
  new code is a build failure from R1 onward**, before any burndown work happens at all.
- **Floors (O-10):** minimum scanned-file count + minimum ledger-row count + per-token
  expected-count table pinned in the ledger. NO total-site floor — site counts
  legitimately fall as waves land, and a second downward ratchet would mask broken scans.
- **`--self-test`:** hermetic, real-gate-via-subprocess, covering: unclassified-site,
  stale-row, count-mismatch, TODO-over-ceiling, ceiling-pin, TEST-FIXTURE-outside-tests
  (resolved-path), DECL-outside-the-pinned-set, macro-family closed-world, marker/floor
  sabotage, masking, function-key directions (above), and the ctest-invocation-shape
  direction (W-1).
- **Wiring:** `RoomResolveCensus` + `RoomResolveCensusSelfTest` ctest entries on every
  preset AND the flat `src/tests/Makefile` recipe (both build systems — the standing
  battery lesson). Test-count delta: **+2**.

**R1 also ships the initial ledger:** machine-generated — production sites `TODO`, test
sites `TEST-FIXTURE`, the resolver-impl and DECL sets pinned, `MAXIMUM_TODO_COUNT` = the
measured count. The generator additionally emits **advisory** suggested classifications
(entry-guard-above-site / occupant-loop / loop-bound patterns) to stdout for wave-brief
planning — advisory only; no machine-minted PROVEN rows (F-12; §3 rule 2). Zero
production code changes in R1.

## 5. Wave structure

- **R1 (this branch):** census tool + gate + ledger + wiring + doc updates (AGENTS.md
  `tools/` bullet, BUILD.md). +2 ctest tests.
- **R2..Rn (per-wave briefs):** classification/guard tranches — library tiers first, then
  the app tier by file family. Each wave: ledger rows with proofs, guards for the
  NOWHERE-reachable minority (red-first per site), ceiling lowered, standing per-wave
  gates, and the measured-zero string sweep over that wave's battery logs (§2).
  Caller-contract proofs cite their guard sites per §3. Wave sizing/order are per-wave
  planning decisions.
- **R-final:** requires ruling **RR-O-1** (§2a) landed first, then: `TODO == 0`; the
  instrumentation pass + extended soak + measured-zero gate (§2); the arm flip (out-of-
  allocation arms; the dummy-window decision input resolved with its fixture mini-census);
  the `room_by_id_total` contract rewrite; deletion of the negative-resolve mudlog (it is
  *replaced by the abort*, not "unreachable" — F-13); the full finalization battery + dual
  adversarial whole-branch reviews.

## 6. Out of scope (explicit)

- **`EXIT(ch,door)->` null-`dir_option` dereference** — distinct defect class, named
  non-goal. NOTE (O-13): the excluded class lives *inside* two RR-family macros
  (`IS_SUNLIT_EXIT`/`IS_SHADOWY_EXIT` deref `dir_option[door]` unchecked); their RR rows
  prove the room-id arguments only, and say so.
- **LS-4 (`obj_data::in_room` store swap)** — sites classified like any others; the swap
  is its own campaign.
- **`room_by_id`'s contract AND its `top_of_world` bound** — untouched; the bound's
  extension-room wrongness (§2) is recorded here as a known limitation and a precondition
  for ever revisiting the forbidden rewrite shape.
- **`zone_by_id`/`obj_index_by_id`** — already nullptr-contract resolvers; nothing to
  retire.

## 7. Verification

The standing cadence, unchanged: per-wave macOS-native build/ctest/monolithic/six-seed/
ASan-on-test-touching-tasks + `rots64` leg + boot goldens; all three censuses exit 0 at
every commit; seed42 golden byte-identical; `make smoke-account` when a wave touches the
login/rent path; i386 battery + six blocking CI jobs at wave finalizations; dual
adversarial whole-branch reviews at program-significant merges (R1, R-final, and any wave
the owner flags); the measured-zero string sweep from the first wave after §2a lands.
Subagent-driven execution; the model-escalation gate re-runs per wave.

## 8. Risks and limits, stated plainly

- **The gate cannot verify proof *semantics*.** §3's vocabulary/length/citation checks
  close the trivially-gameable class; a semantically wrong proof survives until review,
  the measured-zero gate, or the flip. The dual reviews at R-final audit the PROVEN set
  by sampling, with caller-contract rows (the hardest kind) checked citation-by-citation.
- **Two-sided drift** (delete a site and its row in one commit) passes the gate — a
  review question (the F-5-registry stated limit, inherited).
- **The flip depends on an owner ruling (RR-O-1)** that changes operator-visible behavior
  (a god-channel line retires) and overturns a prior binding ruling. It is named, scoped,
  and blocking — not discoverable mid-implementation.
- **Live-game abort risk** is bounded by: proofs + the measured-zero evidence chain + the
  named kill-switch decision input. Post-flip residual risk is a wrong proof converting
  today's silent corruption into a loud crash with a stderr line — the trade the §-top
  ruling accepts.

## 9. Amendment record

The original spec (`91d698b1`) was reviewed by two independent adversarial agents (Opus
O-1..O-14; Fable F-1..F-14), both APPROVE-WITH-CHANGES. Every finding verified against the
tree and applied: the token surface corrected (O-1/F-1 BLOCKER: `world[` + `ASSIGNROOM` +
the alias pair; F-2/F-3); the F17/O-5 collision surfaced as blocking ruling RR-O-1 (O-2
BLOCKER); the fourth silent degrade path and the validity definition (O-3); GUARDED
redefined token-retained + the `room_by_id` bound caveat (O-4/F-5); per-key site counts
against proof inheritance (O-5); the proof-kind vocabulary with citations (O-6/F-4, incl.
replacing the false flagship example); function-key derivation specified fail-closed
(O-7/F-7); `world_room_vnum` reclassified + RESOLVER-IMPL pin corrected (O-8/F-2); the
measured-zero instrumentation gate mandated + kill-switch decision input (F-6/O-9); floors
corrected + ratchet's new-code property stated (O-10/F-10); the `exit(0)` arm demoted to
recursion guard (O-11); count bases labeled raw (O-12/F-9); DECL class added (F-8);
two-room macro proof rule (O-13); occupant-chain proof caveats (F-11/O-14); TEST-FIXTURE
resolved-path check (F-14); mudlog-retirement wording (F-13); advisory classification
output (F-12). No finding was rejected.
