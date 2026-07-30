# LS-3b deferred-MINORs follow-up — design

**Date:** 2026-07-30 (amended same day after dual adversarial spec review — see §5)
**Branch:** `fix/ls3b-review-minors` (off master @`0ee811ae`, 1859 tests)
**Precedent:** the LS-2 deferred-MINORs follow-up (PR #23) and the LS-3a review follow-up (PR #27).

## Context

LS-3b (the LocationSystem Stage 2 representation swap) merged as PR #28. Its dual adversarial
whole-branch reviews deferred three MINOR findings with reasons, recorded in
`.superpowers/sdd/ls3b-t9b-report.md` §4:

- **F-5** (review-2): no defense notices a *new* location store. If a future wave adds e.g.
  `char_data::cached_room_`, none of the wave's defenses fires — the census tokens are
  name-anchored to the known spellings, and the compile-time absence assertion covers only the
  three *old* names.
- **m-14** (review-1): `calc_load_room()`'s bugged-character arm can return −1, producing a
  fully-playing, roomless (`linked nowhere`) player, with a `LEVEL_GOD` mudlog per `room_of()`
  resolve (through `room_by_id_total(-1)`'s room-0 fallback). **Correction, this spec's review
  (O-1):** the original finding's "unguarded light bumps at `act_obj2.cpp:726`/`:769`" claim was
  already false when written — both `do_light` and `do_blowout` early-return on
  `location_of(ch) == NOWHERE` (`act_obj2.cpp:694`/`:736`, guards present since LS-2). The real
  harms are the per-resolve mudlog spam, the room-0 resolution itself, and the latency of the
  state class (see §2.1's latent-defect note).
- **m-15** (review-1): a perf advisory — the ±10% M1 threshold has less margin under host load
  than the baseline's peak-to-peak spread suggests.

**Owner rulings for this PR (2026-07-30):** m-14 resolves by clamping the bugged arm to the
racial start room. m-15 is **SKIPPED** — the review's own conclusion was that nothing in the
tree is defective (the one outlier was host load; the occupancy-vs-cost curve shape, the signal
the baseline names as the one that matters, was unchanged); re-tuning the threshold is a
perf-harness decision for a future wave. m-15 receives a disposition line in the PR description
pointing at where the protocol lives — the M0/M1 harness (`src/entity/location_benchmark.cpp`,
`src/tests/location_benchmark_tests.cpp`) and the T5 baseline table in the LS-3b spec doc — so
the future perf wave does not have to re-derive the finding from a git-ignored review file.

## 1. F-5 — the location-state registry

### 1.1 The registry (`docs/superpowers/location-read-allowlist.md`)

A new "Location-state registry" section in the ledger doc, in **two tables** under its own
anchor markers (the `ALLOW_LIST_TABLE_MARKER` precedent — a marker-introduced table the parser
keys on, so an informational look-alike table elsewhere in the doc cannot shadow it; the
existing allow-list parser stops at the first non-table line and keys on its own header, so the
two tables cannot interfere).

**Table A — entity location stores** ("where is this entity"): every struct member that records
an entity's whereabouts. Each row carries: member spelling, declared-at, kind,
**representation (rnum / vnum / handle / not-a-location)** — vnum-vs-rnum confusion, not name
obscurity, is this program's recurring defect class (R2, the LS-3a `save_char` riders, and m-14
itself) — and coverage disposition:

| Store | Declared at | Kind | Repr | Coverage |
|---|---|---|---|---|
| `char_data::ls_location_id_` | `core/character.h:862` | live (private handle) | rnum | TOKEN `ls_location_id_` |
| `char_data::ls_next_in_room_` | `core/character.h:898` | live (private chain link) | handle | TOKEN `ls_next_in_room_` |
| `room_data::ls_first_occupant_` | `core/room.h:126` | live (private chain head) | handle | TOKEN `ls_first_occupant_` |
| `char_data::ls_load_room_vnum_` | `core/character.h:353` | live (login-window channel) | vnum | TOKEN `ls_load_room_vnum_` — promoted by this PR, see 1.2 |
| `char_data::specials.was_in_room` | `core/character.h:340` | live (linkdead stash) | rnum | UNTRACKED-BY-DESIGN — settled plain-`int` stash, NOT the representation (ledger R-C5 text); tokening it would mint ~15 permanent annotations with no honest reason prefix and no burndown path (review O-3/F-3) |
| `char_special2_data::load_room` | `core/types.h` | PERSISTED (playerfile) | vnum (rnum transiently in the equip_lost window) | UNTRACKED-BY-DESIGN (pervasive; a token needs its own census — see 1.4) |
| `affected_type::modifier` under `SPELL_BEACON` | `core/types.h:719` | PERSISTED (playerfile) | rnum | UNTRACKED-BY-DESIGN (generic field name — `obj_affected_type::modifier` at `:384` is a second member of the same spelling, so no name-anchored pattern can single it out; guarded instead by the O-7 two-sided load/save guard) |
| `obj_data::in_room` | `core/object.h:165` | deferred (LS-4 campaign) | rnum | TOKEN (the three `in_room` accessor patterns) |
| `shop_data::in_room` | `app/shop.cpp:63` | not-a-location (shop identity) | vnum | TOKEN (accessor patterns) + `not-a-location` annotations |

The compile-time absence assertion (`entity/placement.cpp`) covers **none** of these rows — it
witnesses only the absence of the three retired public spellings (`in_room`/`next_in_room`/
`people`); the registry records that explicitly so the two defenses are never conflated (O-14).

**Table B — other room-id carriers**: storage that carries a room id without being an entity's
whereabouts. These are IN the closed world (the standing rule covers them) but are not location
stores; each row carries spelling, declared-at, representation, and a one-line reason:

| Carrier | Declared at | Repr | Class |
|---|---|---|---|
| `room_direction_data::to_room` | `core/types.h:395` | rnum | world topology (exit target; the LS-2 review's dangling-fixture-pointer class) |
| `shop_data::stock_room` | `app/shop.cpp:64` | vnum | object-parameter room reference (LS-2 T3d resolver trap) |
| `obj_data::obj_flags.value[0]` under `ITEM_LEVER` | `core/object.h` | vnum | object-parameter room reference (resolved at `act_move.cpp:1829`) — included because `SPELL_BEACON` is included; same discriminated-generic-field class |
| `target_data::ptr.room` | `core/types.h:248` | handle | transient targeting (queued-command target) |
| the boot-computed room-id globals: `r_mortal_start_room[]`, `r_mortal_idle_room[]`, `r_immort_start_room`, `r_frozen_start_room`, `r_retirement_home_room`, `mortal_maze_room[][2]`, **`r_bugged_start_room`** | `core/consts.cpp` / `world/db_world.cpp` | rnum — **except `r_bugged_start_room`, which is a VNUM despite its `r_` spelling** | boot/world configuration |

`r_bugged_start_room`'s row is the registry's own justification: it is a room-id store whose
representation was mislabeled by its spelling — exactly the defect class §2 of this spec fixes —
and a members-only registry would have excluded it (review O-4/F-1). Table B's row records the
correct representation permanently.

**The standing rule, minted in the ledger:** any future **storage** — struct member, file-scope
global, or persisted field — that carries a room id (rnum, vnum, or handle) MUST be added to the
registry in the same commit that introduces it: Table A with a coverage disposition if it
records an entity's whereabouts, Table B with a class if not. A TOKEN disposition additionally
requires the matching `TOKEN_PATTERNS` entry.

### 1.2 The token promotion: `ls_load_room_vnum_` (not `was_in_room`)

The original draft promoted `was_in_room`. Both spec reviews independently overturned that
choice (O-3, F-2/F-3), and the measured evidence agrees:

- `was_in_room`: ~15 production lines across 7 files would newly flag; the 6 pure read sites
  (`comm.cpp:1301/:1306`, `limits.cpp:576/:595/:599/:600`) fit **none** of the ten authorized
  reason prefixes honestly (the ledger's own R-C5 text says `was_in_room` is not the
  representation, so `representation-*` would be abuse); every annotation would be permanent —
  a standing exemption wearing a coverage costume, in tension with the prefix burndown rule.
- `ls_load_room_vnum_`: a genuine LocationSystem private store — the fourth sibling of the
  three already-tokenized `ls_*` members — whose ACCESSOR-GATED prose claim currently has zero
  mechanical enforcement. A bare-word `\bls_load_room_vnum_\b` token costs exactly **one**
  annotation (the declaration, `character.h:353`, `representation-decl` — the same prefix its
  three siblings carry); the only real access lines (`placement.cpp:337/:342`) sit inside a
  whole-file-exempt representation owner. The spelling is unique tree-wide.

So: `TOKEN_PATTERNS` grows ten → **eleven** with `ls_load_room_vnum_`; `was_in_room` stays
untracked with its reason recorded in Table A. No new reason prefix is needed; the authorized
prefix list stays at ten.

### 1.3 The census cross-check

Two checks, placed carefully with respect to `--self-test`'s hermeticity (`run_self_test()`
builds a synthetic tree and synthetic ledger and never touches the repository — its docstring
guarantees it; the cross-check must not silently break that, review O-6/F-6):

**(a) The consistency assertion runs in `--check`** (the merge-gating mode, which already reads
the real ledger through the `--root`-hardened path resolution PR #23 added). After the normal
scan, `--check` parses the registry tables out of the real ledger doc and asserts,
bidirectionally:

1. Every Table-A row with a TOKEN disposition names a token present in `TOKEN_PATTERNS`,
   **and that token's pattern matches a synthesized probe derived from the row** — the bare
   member spelling for bare-word patterns, and accessor-prefixed probes (`x->` + spelling,
   `x.` + spelling, `x::` + spelling, any one sufficing) for accessor-anchored patterns. (A
   literal "pattern matches the member's spelling" check is unsatisfiable for the three
   `in_room` accessor tokens — the anchor is their point; review O-5/F-5.)
2. Every `TOKEN_PATTERNS` entry maps to at least one Table-A row, or appears in a named
   exemption set inside the script: structural tokens (`world[`, `token-paste`/`##`) and
   retired-spelling guards (`next_in_room`, `people` — kept to catch reintroduction of the
   pre-LS-3b member names, which no current store spells). The exemption's validity condition
   is stated in the ledger: it holds while no struct anywhere spells those members; a future
   struct reusing a retired spelling fires the token per-line but must also be re-adjudicated
   against the registry (F-9).
3. Fail-closed floors: the parse must find both marker-anchored tables and at least the
   current row counts (the `MINIMUM_SCANNED_FILE_COUNT` precedent), so the check can never
   pass by failing to find the registry.

**(b) `--self-test` proves the checker, hermetically.** The registry parse is factored as a
pure function over text (`parse_registry(text)`), and the standing self-test cases drive it and
the consistency assertion against **synthetic registry fixtures** checked into the `SELF_TEST_*`
constants (the `SELF_TEST_LEDGER` precedent) — never by mutating the real doc. Standing
sabotage cases, checked in: a row claiming a nonexistent token → fails; a token with neither
row nor exemption entry → fails; a registry section missing/unparseable/below-floor → fails;
an accessor-anchored TOKEN row (exercising the probe synthesis) → passes. Any probe against
the real ledger during implementation is a one-time non-vacuity demonstration *in addition to*,
never instead of, the standing cases — the probe-and-revert pattern alone is exactly what
PR #23 retired.

`--self-test` case count grows from 56; exact count recorded at implementation.

### 1.4 What F-5's closure does NOT claim

Mechanical detection of an arbitrarily-named new store remains impossible; that is stated in
the ledger next to the standing rule. The check is also closed against **one-sided** drift
only: a future commit that deletes a token and downgrades the matching registry row in the same
edit passes both directions — a coordinated two-sided edit is a review question, not a gate
question (O-13). The defense is: the registry is the declared closed world, the `--check`
assertion keeps gate and registry consistent with each other, and the standing rule makes "add
a store without registering it" a reviewable process violation rather than a silent gap.

Explicitly out of scope: a `load_room` token (pervasive; needs its own census) and any
struct-parsing/type-based detection scheme (judged impossible for arbitrary names by the
originating review; a heuristic would be theater).

### 1.5 Documentation reconciliation (part of T1, not optional)

The token promotion makes standing text stale; T1 updates all of it in the same commit
(re-creating T9b's m-7 stale-count class in the very PR that closes the deferral set would be
absurd — O-8/F-7):

- `docs/superpowers/location-read-allowlist.md`: the "One named exclusion remains untracked by
  construction (`was_in_room`)" paragraph (~:77-90) — rewritten to cite Table A's row; the
  `not-a-location` prose inventory (~:203-207); the token-count references.
- `tools/location_read_census.py`: the module docstring's untracked-exclusion narrative
  (~:16-27); the `TOKEN_PATTERNS` comment block.
- `AGENTS.md` (the `tools/` bullet: token count ten → eleven, self-test count) and
  `docs/BUILD.md` (:1168/:1200 region counts).

## 2. m-14 — clamp the bugged arm

### 2.1 The defect

`r_bugged_start_room` is statically initialized to `1152` (`src/core/consts.cpp:2554`) and never
recomputed at boot (`check_start_rooms()` recomputes every *other* start room and never touches
it); despite the `r_` prefix it is handed to `real_room()` (a vnum→rnum lookup) at
`objsave.cpp:622`, in the bugged-character arm that sits AFTER the function's `load_room < 0`
clamp. When vnum 1152 does not resolve in the loaded world data, `calc_load_room()` returns −1
and `load_character()` places a `CON_PLYNG` character linked nowhere. This is the only
−1-producing arm left in the function: every earlier arm is either covered by the general clamp
or assigns boot-clamped globals (`check_start_rooms()` forces every `r_mortal_start_room[]`
entry ≥ 0 and falls the immortal/frozen rooms back to `r_mortal_start_room[0]`), and the
level-0 arm after it assigns a boot-clamped value. Verified independently by both spec reviews.

**Latency note (O-12):** vnum 1152 *does* resolve in the shipped world data
(`lib/world/wld/11.wld`, `#1152 *** Warning bugged character ***`, loaded per the boot golden's
zone list). m-14 is therefore a **latent robustness defect, not a live one** — the clamp buys
resilience against a world edit that deletes or renumbers 1152, and against dev/test worlds
that lack it. This also sets expectations for §3: `make smoke-account` exercises the login path
with 1152 present and cannot observe the change.

### 2.2 The fix (owner ruling)

In `calc_load_room()`'s bugged arm: if `real_room(r_bugged_start_room)` is negative, fall back
to `r_mortal_start_room[GET_RACE(ch)]` — the identical expression the general clamp at
`:617-618` already uses. Bugged characters still go to the bugged room whenever it resolves;
the roomless-player class is retired at its only production source.

Two deliberate choices, recorded (O-10/O-11):

- **Accepted asymmetry:** in the degenerate (unresolvable-bugged-room) case, a bugged immortal,
  race-0, or frozen character falls back to the *mortal racial* start room, overriding what
  their own arms would have chosen. All three outcomes are strictly better than −1, the
  degenerate case requires a broken world file to reach at all, and mirroring the branch
  structure would triple the fix's surface for no live behavior. Accepted as-is.
- **Local clamp, not a moved general clamp:** relocating `:617-618` below the bugged/level-0
  arms is arithmetically equivalent and one line — but it would silently change what every
  *future* arm added after it inherits, and the local clamp keeps the bugged arm's intent
  legible at the site. The local form is preferred.

A one-line comment at `consts.cpp:2554` records that `r_bugged_start_room` holds a VNUM despite
its spelling (the rename itself stays out of scope — cosmetic churn across tests). Table B's
registry row records the same fact permanently.

### 2.3 Tests — red-first, with the discrimination traps closed

The spec review found the naive "same fixtures, inverted expectation" flip **cannot
discriminate** (O-2, independently F-8): `ScopedStartRooms` pins `kRacialStartRnum = 0`, and
`room_data::operator[]`'s negative-index fallback resolves to rnum 0 too — so post-fix
"clamped to racial start" and pre-fix "−1 resolved through the room-0 fallback" are the same
observable room. And the fixture defaults `r_bugged_start_room = room_vnum_for(kRacialStartRnum)`,
making the naive positive control a tautology. Therefore:

- **The two flipped tests** (`load_room_placement_tests.cpp:1770` region, `:3205` region — both
  force the arm via `r_bugged_start_room = 999999`) must assert against a racial start room
  whose rnum is **distinct from 0** (per-test override of `r_mortal_start_room[race]`, or a
  fixture parameter — implementer's choice), so the flipped assertion is red against the
  unclamped arm. Red-first evidence is mandatory: each flipped assertion demonstrated failing
  against the pre-fix line.
- **The positive control** (resolvable bugged room is still used) must use a bugged-room vnum
  resolving to an rnum distinct from **both** the racial start rnum and rnum 0, so it fails if
  the clamp overshoots (always-racial-start) — the O-I3 vacuous-control class, avoided by
  construction.
- **Teardown becomes load-bearing again (leak class):** post-fix, both flipped tests link a
  stack `char_data` into a real room's occupant chain. The ROW-1 test's trailing unlink —
  documented in-tree as "NOW A NO-OP" — must be re-pointed at the room the character actually
  lands in, or it leaks a stack pointer into a process-global chain: the exact
  `DoRescue`/`waiting_list` class that SIGSEGV'd the LS-2 i386 monolithic battery and that
  ctest structurally cannot see. The monolithic + ASan gates in §3 backstop this; the
  obligation is named here so it is designed, not re-learned.
- **The B-1 witness survives:** `PostLoginSaveOfABuggedCharacterPersistsNowhereThroughTheReal
  LoadCharacter` (the T9b BLOCKER B-1 red-first witness) has its location assertions inverted
  by the clamp, but its discriminating property — the VNUM channel is retired at placement
  regardless of placement outcome — must be preserved and **re-demonstrated red** against a
  reverted channel-retirement after the flip.
- ASan applies (test files are touched).

### 2.4 In-tree text that becomes counterfactual (part of T2)

Three rationale sites narrate the −1 arm's reachability and are updated with the fix (O-9):
`interpre.cpp:3805-3811` (whose `objsave.cpp:588-589` citation is stale independently of this
PR), the channel-lifetime block at `objsave.cpp:517-540`, and the ROW-1/B-1a test rationales
(`load_room_placement_tests.cpp:1710-1745`, `:3165-3175`). The flipped tests' own narration is
rewritten as part of the flip.

### 2.5 Explicitly NOT touched

- `char_to_room`'s NOWHERE semantics (SETTLED — O-5: NOWHERE means linked nowhere).
- The `act_obj2.cpp` light-bump sites: **already guarded** at `:694`/`:736`
  (`location_of(ch) == NOWHERE` early returns) — the original m-14 claim of unguarded
  reachability was false at HEAD; nothing to do (O-1).
- The general roomless-resolution behavior (`room_by_id_total(-1)` → room 0 + mudlog): the
  operator[] fallback-retirement campaign owns it.
- `r_bugged_start_room`'s spelling (comment + registry row only, per §2.2).

## 3. Verification

The standing cadence (AGENTS.local.md):

- Per-change: macOS-native build + `ctest --preset macos-arm64` + monolithic single-process run
  + six-seed shuffle + both censuses (`location_read_census.py --check`/`--self-test`,
  `string_view_census.py --check`) + boot golden; ASan preset (test files are touched);
  `rots64` leg + its boot golden.
- **`make smoke-account` is MANDATORY** — `calc_load_room()` is squarely on the login/rent path
  (AGENTS.md account/login rule + R-A2) — while noting it cannot observe the m-14 change
  itself (§2.1's latency note); it guards against regression, not for the fix.
- Finalization, before merge: the full i386 battery (`scripts/i386-battery.sh`), all six
  blocking CI jobs green. Known traps: the Mach-O-in-bin restore
  (`cp /rots/build/ageland /rots/bin/ageland` in-container after a host smoke-account) and
  future-mtime normalization before the battery.
- Adversarial review before merge; merge is the owner's call.

Expected movement: the `--self-test` growth is NOT a ctest count change
(`LocationReadCensusSelfTest` is one ctest test however many internal cases it drives); the
m-14 work flips two existing tests in place and adds at least one positive control, so the
ctest total moves by +1 minimum. Goldens: no boot-golden or seed42 drift is expected — the
boot golden never logs in (so `calc_load_room()` is never called) and the seed42 transcript
never reaches the login path; any drift is a bug in the change.

## 4. Execution shape

Implementation is subagent-driven (Sonnet tier per the model-escalation gate: 0/4 criteria
tripped), two independent tasks — the order is arbitrary (T2 introduces no tracked-token
lines; T1's single annotation lands in `character.h`):

1. **T1 — F-5**: ledger registry section (Tables A + B, anchor markers, standing rule) +
   the `--check` consistency assertion + `parse_registry` + standing synthetic self-test cases
   + the `ls_load_room_vnum_` token promotion (one annotation) + the §1.5 doc reconciliation.
2. **T2 — m-14**: red-first test flips with the §2.3 discrimination mandates, the positive
   control, the clamp, and the §2.4 comment updates.

## 5. Amendment record

The original spec (`0d334338`) was reviewed by two independent adversarial agents (Opus:
O-1..O-16; Fable: F-1..F-11), both APPROVE-WITH-CHANGES. Every MAJOR was verified against the
tree and applied: O-1 (false light-bump claim corrected), O-2/F-8 (test-plan discrimination
collapse + vacuous control), O-3/F-2/F-3 (promotion swapped `was_in_room` →
`ls_load_room_vnum_`), O-4/F-1 (registry widened: Table B, representation column, storage-not-
member standing rule), O-5/F-5 (probe-synthesis rule), O-6/F-6 (hermeticity split: `--check`
assertion + synthetic self-test fixtures). MINORs/NOTEs applied: F-4/O-7 (citations), O-8/F-7
(§1.5), O-9 (§2.4), O-10/O-11 (§2.2 choices), O-12 (§2.1/§3), O-13 (§1.4), O-14 (§1.1),
O-15 (m-15 pointer), F-9 (§1.3 exemption condition), F-11/O-16 (§4 ordering). No finding was
rejected.
