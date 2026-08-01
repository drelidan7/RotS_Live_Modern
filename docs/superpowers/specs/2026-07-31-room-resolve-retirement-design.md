# The room-resolve retirement program ("RR") — design

**Date:** 2026-07-31
**Branch (Wave R1):** `arch/rr1-census` (off master @`a70407a7`, 1860 tests)
**Status:** program design + Wave R1 spec. Waves R2+ get per-wave briefs off this document
(the LocationSystem precedent: one program design doc, per-wave operative specs).
**Owner ruling (2026-07-31):** end-state failure semantics = **proof-driven burndown to
abort** — no behavior change at any site before its proof or guard lands; the fallback arms
flip to tripwire aborts only when the burndown reaches zero.

## 1. The defect class

`room_data::operator[]` (`src/world/db_world.cpp:2065`) is a *total* function over invalid
input, by three graceful-degrade arms:

| Input | Behavior today |
|---|---|
| `i < 0` | `mudlog("world[] called for negative room number.", NRM, LEVEL_GOD, TRUE)` + **return `world[0]`** |
| `i >= BASE_LENGTH`, beyond all extensions | mudlog (LEVEL_GRGOD) + **return `world[r_immort_start_room]`** |
| same, but `i == r_immort_start_room` | **`exit(0)`** — a clean-shutdown exit for a bad room id |
| `BASE_WORLD == nullptr` | `abort()` (already a tripwire — LS-3a hardened it; not part of this campaign) |

`room_by_id_total()` deliberately preserves this contract ("graceful total function",
`db_world.cpp:292-300`), and `room_of(ch)` = `room_by_id_total(location_of(ch))`
(`entity/placement.cpp:354`). Consequence: every unguarded `room_of`/macro/direct-resolver
site silently reads **room 0** when handed a roomless character (`location_of == NOWHERE`),
or the immortal start room for an out-of-range id — the exact corruption surface m-14
exposed (light bumps, occupant reads, per-resolve mudlog spam against room 0). The LS-3b
deferred-MINORs follow-up retired the one known *producer* (the bugged login arm); this
campaign retires the *consumer-side tolerance* that made the producer dangerous.

Measured at `a70407a7` (production, `src/tests` excluded; the R1 census makes these exact
and total): ~297 `room_of(` sites, ~221 `EXIT(` sites, ~252 direct `room_by_id_total(`
sites, plus the macro family below. Sites overlap (macros expand to `room_of`/
`room_by_id_total`); the census counts *surface* spellings, each classified once.

**The resolver-expanding macro family** (`src/utils.h`, enumerated at HEAD — R1's census
re-derives this list from the macro bodies rather than trusting this table): `EXIT` (:721),
`OUTSIDE` (:719), `SUN_PENALTY` (:517) over `room_of`; `IS_DARK` (:328), `IS_LIGHT` (:332,
via `IS_DARK`), `IS_SUNLIT` (:334, via `IS_LIGHT`), `IS_SUNLIT_EXIT` (:511),
`IS_SHADOWY_EXIT` (:514), `IS_WATER` (:796) over `room_by_id_total`. A macro call site is a
resolver site even though no resolver token appears on the line — the census must track the
macro names as first-class tokens.

## 2. End-state semantics (the ruling, made precise)

When the burndown reaches zero `TODO` rows, the final wave flips the two graceful arms and
the `exit(0)` arm of `operator[]` to **tripwire aborts** (`fprintf(stderr, ...)` + `abort()`,
matching the existing `BASE_WORLD` arm's idiom), and rewrites `room_by_id_total`'s contract
comment: it stays total over the *allocated* range (base + extensions) and becomes a tripwire
over invalid input. `room_by_id`'s nullptr contract is untouched throughout — it is already
the correct "absent is normal" spelling, and `GUARDED` conversions rewrite onto it.

Until that flip, **zero behavior change**: classification is ledger rows; only `GUARDED`
sites change code, each red-first-tested per the standing coverage-gap rule.

**No death tests** (standing rule since the blocker-buster wave): the abort arms are not
unit-tested by dying. Their unreachability IS the census's claim, witnessed by the zero-TODO
gate plus the full standing battery (boot goldens, characterization, smoke-account, monolithic
+ shuffle, i386) at the flip wave's finalization. The flip wave additionally runs an extended
native soak (boot + scripted command sweep) as its own gate — its brief defines it.

## 3. Program architecture — classify in place, don't rename

The burden unit is a **proof obligation**, not a call rewrite. `room_of(ch)` is the correct
post-campaign spelling for a placed character; renaming ~770 sites to a "checked" API would
be churn without information. Instead every site gets exactly one ledger classification:

- **`PROVEN`** — the input cannot be invalid, with the proof *stated in the row*: an entry
  guard (`location_of(ch) == NOWHERE` early return above the site), a caller contract (only
  reachable from the command interpreter, which rejects roomless actors), an
  occupant-chain iteration (an occupant is by invariant placed — the LS-3b O-5 invariant's
  first half), a loop bound (`rnum` ranges over `0..top_of_world`), or a same-function
  dominating resolve. Proofs are typically **per-function**: one entry guard proves every
  site in the function, so one row may cover many sites (the row lists them).
- **`GUARDED`** — this campaign edited the site: an explicit `location_of != NOWHERE` /
  `room_by_id(...) == nullptr` guard with a decided absent-behavior, landed red-first with
  a named test. The row cites the wave and test.
- **`TEST-FIXTURE`** — `src/tests/` sites. Tests deliberately probe invalid ids and manage
  their own worlds; they are enumerated (visible, counted, never a blind spot — the R-B8
  lesson: no directory exclusions) but carry no proof obligation. The class is legal only
  under `src/tests/`; the gate rejects it elsewhere.
- **`RESOLVER-IMPL`** — the implementation's own accesses (`placement.cpp`'s wrappers,
  `db_world.cpp`'s `_impl`s and `operator[]` recursion, `world_room_vnum`). Closed set,
  gate-pinned by file+count.
- **`TODO`** — not yet classified. The ratchet class: its count may only decrease.

**Two structural rules learned from the LS program, inherited day one:**
1. The classification ledger is marker-anchored and parsed fail-closed (the M10/PR-#23/W-1
   lesson chain): anchored tables, row-count floors, no directory exclusions, self-test with
   sabotage directions, and the consistency check enforced under BOTH census invocation
   shapes (bare and `--exceptions`-style; R1's gate takes the W-1 path-equality design from
   the start).
2. The gate is line-based and does no reachability analysis of its own; the ledger carries
   the proofs and the adversarial reviews audit them (the location gate's "annotation
   carries it" doctrine). A `PROVEN` row with a bogus proof is a review finding, not a gate
   finding — stated plainly in the ledger so the gate is never mistaken for a verifier.

## 4. The census tool and gate (Wave R1's deliverable)

**`tools/room_resolve_census.py`** — a sibling of `location_read_census.py`, sharing its
architecture (and hard-won hardening) but a separate tool: different tokens, different
ledger, different question (input-validity proof vs representation access).

- **Tokens:** `room_of(`, `room_by_id_total(`, and the macro family of §1, re-derived by the
  R1 implementation from `utils.h`'s macro bodies (any macro whose expansion reaches a
  resolver). Bare-word, comment/string-masked (reuse the location census's masking code by
  extraction into a shared helper or by copy — R1 decides; a shared module must not weaken
  either tool's self-test).
- **Ledger:** `docs/superpowers/room-resolve-ledger.md` (new tracked doc): the program
  contract prose plus one marker-anchored classification table
  (`<!-- ROOM-RESOLVE-CLASSIFICATION -->`), rows = (site file:line-range or
  file:function, token(s), class, proof/citation). Line numbers are advisory; the row key
  is file + function + token. The census reconciles scan vs ledger both directions:
  unclassified site → fail; ledger row matching nothing → fail (stale row).
- **The ratchet:** `--check` fails if the `TODO` count exceeds the checked-in
  `MAXIMUM_TODO_COUNT` literal; each conversion wave lowers the literal in the same commit
  that drains rows (the `MINIMUM_SCANNED_FILE_COUNT` two-edit doctrine, inverted). Floors:
  minimum scanned-file count; minimum total-site count (a scan that finds half the tree must
  not pass).
- **`--self-test`:** hermetic (synthetic tree + synthetic ledger; never reads repo docs),
  driving the real gate end to end via subprocess: unclassified-site direction, stale-row
  direction, TODO-above-maximum direction, TEST-FIXTURE-outside-tests direction,
  marker/floor sabotage, masking checks, and the ctest-invocation-shape direction (W-1's
  standing lesson).
- **Wiring:** a `RoomResolveCensus` ctest test on every preset + the flat
  `src/tests/Makefile` `tests` recipe (BOTH build systems — the recurring
  new-linkcheck/new-test-file battery lesson), same shape as `LocationReadCensus`.

**R1 also ships the initial ledger:** machine-generated, every production site `TODO`, test
sites `TEST-FIXTURE`, the resolver-impl set `RESOLVER-IMPL`, `MAXIMUM_TODO_COUNT` set to the
measured count. Zero production code changes in R1 — the wave is tooling + inventory,
consumer-free, and the tree's behavior is byte-identical.

## 5. Wave structure

- **R1 (this branch):** census tool + gate + ledger + wiring + doc updates (AGENTS.md
  `tools/` bullet, BUILD.md). Test-count delta: **+2** — `RoomResolveCensus` (`--check`) and
  `RoomResolveCensusSelfTest` (`--self-test`) as two ctest entries, the
  `LocationReadCensus`/`LocationReadCensusSelfTest` precedent exactly.
- **R2..Rn (per-wave briefs):** classification/guard tranches by subsystem — natural cuts:
  the library tiers first (`entity`/`world`/`combat`/`pathfind`/`script`/`olc`, smallest and
  best-invariant-covered), then the app tier by file family (`act_*`, `interpre`/`comm`,
  `objsave`/`shop`, `spec_*` already lib-side). Each wave: classify (ledger rows with
  proofs), guard the NOWHERE-reachable minority (red-first tests per site), lower
  `MAXIMUM_TODO_COUNT`, full standing per-wave gates. Wave sizing and order are planning
  decisions per wave, not fixed here.
- **R-final:** `TODO == 0` → the §2 flip (three arms → tripwire aborts), the
  `room_by_id_total` contract-comment rewrite, retirement of the negative-resolve mudlog
  (unreachable), the extended soak gate, and the full finalization battery + dual
  adversarial whole-branch reviews per the LocationSystem convention.

## 6. Out of scope (explicit)

- **`EXIT(ch,door)->` null-`dir_option` dereference** — a distinct defect class (absent
  exits are *normal*; the hazard is unchecked `->to_room` after a null `dir_option`). The
  R1 ledger records it as a named non-goal so no reviewer mistakes it for a missed scope.
- **LS-4 (`obj_data::in_room` store swap)** — object-side resolver *sites* are classified
  like any others; the store swap remains its own future campaign.
- **`room_by_id`'s nullptr contract** — already correct; untouched.
- **`zone_by_id`/`obj_index_by_id`** — already nullptr-contract resolvers with no graceful
  fallback; nothing to retire.

## 7. Verification

The standing cadence, unchanged: per-wave macOS-native build/ctest/monolithic/six-seed/
ASan-on-test-touching-tasks + `rots64` leg + boot goldens; all three censuses
(`location_read_census`, `string_view_census`, now `room_resolve_census`) exit 0 at every
commit; seed42 characterization golden byte-identical; `make smoke-account` when a wave
touches the login/rent path; i386 battery + six blocking CI jobs at wave finalizations;
dual adversarial whole-branch reviews at program-significant merges (R1, R-final, and any
wave the owner flags). Subagent-driven execution, implementation pinned per the
model-escalation gate (R1 tooling: Sonnet-tier; the gate re-runs per wave).

## 8. Risks and limits, stated plainly

- **The gate cannot verify proofs.** A wrong `PROVEN` row survives until review or the
  final flip's soak exposes it. Mitigations: proofs are text in a reviewed tracked doc;
  the dual adversarial reviews at R-final audit the `PROVEN` set by sampling; the flip
  itself is the last, loudest detector — and by the ruling, it lands only after the whole
  program's evidence chain, not before.
- **Two-sided drift** (delete a site and its row in one commit) passes the gate — a review
  question, not a gate question (the F-5 registry's stated limit, inherited).
- **Live-game abort risk** is bounded by the ruling itself: no abort is reachable except
  through a site the ledger claims proven/guarded, and the arms flip only at zero TODO with
  the full battery green. Residual risk after the flip is a wrong proof — see above — and
  the abort converts what is today a silent corruption into a loud crash with a stderr
  line; the owner accepts that trade by the §2 ruling.
