# LS-3b deferred-MINORs follow-up — design

**Date:** 2026-07-30
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
  fully-playing, roomless (`linked nowhere`) player with a `LEVEL_GOD` mudlog per `room_of()`
  resolve and unguarded light bumps reachable at `act_obj2.cpp:726`/`:769`.
- **m-15** (review-1): a perf advisory — the ±10% M1 threshold has less margin under host load
  than the baseline's peak-to-peak spread suggests.

**Owner rulings for this PR (2026-07-30):** m-14 resolves by clamping the bugged arm to the
racial start room. m-15 is **SKIPPED** — the review's own conclusion was that nothing in the
tree is defective (the one outlier was host load; the occupancy-vs-cost curve shape, the signal
the baseline names as the one that matters, was unchanged); re-tuning the threshold is a
perf-harness decision for a future wave. m-15 receives a disposition line in the PR description
and no code.

## 1. F-5 — the location-state registry

### 1.1 The registry table (`docs/superpowers/location-read-allowlist.md`)

A new "Location-state registry" section in the ledger doc enumerates every known location store
at HEAD — the complete inventory, not just the token-covered subset:

| Store | Declared at | Kind | Coverage |
|---|---|---|---|
| `char_data::ls_location_id_` | `core/character.h:862` | live (private handle) | TOKEN `ls_location_id_` |
| `char_data::ls_next_in_room_` | `core/character.h:898` | live (private chain link) | TOKEN `ls_next_in_room_` |
| `room_data::ls_first_occupant_` | `core/room.h:126` | live (private chain head) | TOKEN `ls_first_occupant_` |
| `char_data::ls_load_room_vnum_` | `core/character.h:353` | live (login-window VNUM channel) | ACCESSOR-GATED (`stash_load_room_vnum`/`peek_load_room_vnum` only) |
| `char_data::specials.was_in_room` | `core/character.h:340` | live (linkdead stash) | TOKEN — see 1.2 |
| `char_special2_data::load_room` | `core/types.h` | PERSISTED (playerfile) | UNTRACKED-BY-DESIGN (field name `load_room` is accessed via `GET_LOADROOM`/direct; a bare pattern would be minted only if a census shows it cheap — see 1.4) |
| `affected_type::modifier` under `SPELL_BEACON` | `core/types.h` | PERSISTED (playerfile, room rnum) | UNTRACKED-BY-DESIGN (generic field name; unmatchable by any name-anchored pattern — guarded instead by the O-7 two-sided load/save guard) |
| `obj_data::in_room` | `core/types.h` | deferred (LS-4 campaign) | TOKEN (the `in_room` accessor patterns) |
| `shop_data::in_room` | app tier | not-a-location (shop VNUM) | TOKEN (the `in_room` accessor patterns) + `not-a-location` annotations |

Line numbers in the table are informational; the registry keys on the **member spelling**, which
is what the census can check.

**The standing rule, minted in the ledger:** any future member of `char_data`/`room_data`/
`obj_data` (or any persisted field) that carries a room id — rnum, vnum, or handle — MUST be
added to this registry in the same commit that declares it, with a coverage disposition; a
TOKEN disposition additionally requires the matching `TOKEN_PATTERNS` entry. The gate cannot
mechanically detect an arbitrary new name (review-2's own finding); what it CAN do is fail
closed against its own ledger going stale, which is what 1.2 buys.

### 1.2 The census cross-check (`tools/location_read_census.py --self-test`)

`--self-test` gains a closed-world cross-check that parses the registry table out of the real
ledger doc and asserts, bidirectionally:

1. **Every registry row with a TOKEN disposition names a token that exists in
   `TOKEN_PATTERNS` and whose pattern matches the member's spelling.** A registry row whose
   token was dropped or renamed fails the self-test.
2. **Every `TOKEN_PATTERNS` entry maps to at least one registry row, or appears in a named
   exemption set inside the self-test** — structural tokens (`world[`, `token-paste`/`##`) and
   retired-spelling guards (`next_in_room`, `people` — tokens kept to catch reintroduction of
   the pre-LS-3b member names, which no current store spells). A token minted without either a
   registry row or an exemption entry fails the self-test. (The three `in_room` accessor tokens
   map to the `obj_data::in_room`/`shop_data::in_room` rows, so they need no exemption.)

Deviation from the reviewer's sketch, recorded honestly: F-5's parenthetical proposed asserting
"`TOKEN_PATTERNS` covers every member enumerated" — literally unsatisfiable at HEAD, because
`ls_load_room_vnum_`, `was_in_room`, `load_room`, and the beacon `modifier` have no matching
token today (the channel is accessor-gated by design; `modifier` is unmatchable). The coverage-
disposition column is the workable form of the same closed-world idea. As part of this PR,
`was_in_room` gets promoted to TOKEN coverage: a bare-word `\bwas_in_room\b` pattern is cheap,
the spelling is unique tree-wide (same argument as `next_in_room`), and it converts one
UNTRACKED row into a checked one. `TOKEN_PATTERNS` goes ten → **eleven**; every existing
`was_in_room` production site gets a census run and, where flagged, an `LS1-ALLOW` annotation
with an existing reason prefix (expected: the linkdead stash sites are representation-adjacent;
if a new prefix proves necessary the ledger records it, but the default is to reuse).

3. **Sabotage variants**, per the standing non-vacuity rule: (a) a registry row claiming a
   token that does not exist → self-test fails; (b) a `TOKEN_PATTERNS` entry with no registry
   row → fails; (c) the registry section deleted or unparseable from the ledger → fails (the
   parse must find ≥ the current row count, the same fail-closed floor idea as
   `MINIMUM_SCANNED_FILE_COUNT`); (d) restore, `cmp`-verified.

`--self-test` case count grows from 56; exact count recorded at implementation.

### 1.3 What F-5's closure does NOT claim

Mechanical detection of an arbitrarily-named new store remains impossible; that is stated in
the ledger next to the standing rule, so the registry is never mistaken for a stronger guarantee
than it is. The defense is: the registry is the declared closed world, the self-test keeps the
gate and the registry consistent with each other, and the standing rule makes "add a store
without registering it" a reviewable process violation rather than a silent gap.

### 1.4 Explicitly out of scope

Minting bare-word patterns for `load_room` (pervasive, needs its own census) and any
struct-parsing/type-based detection scheme (review-2 already judged it impossible for arbitrary
names; a heuristic would be theater). Either can be a future wave if the registry proves
insufficient.

## 2. m-14 — clamp the bugged arm

### 2.1 The defect

`r_bugged_start_room` is statically initialized to `1152` (`src/core/consts.cpp:2554`) and never
recomputed at boot; despite the `r_` prefix it is handed to `real_room()` (a vnum→rnum lookup)
at `objsave.cpp:622`, in the bugged-character arm that sits AFTER the function's `load_room < 0`
clamp. When vnum 1152 does not resolve in the loaded world data, `calc_load_room()` returns −1
and `load_character()` places a `CON_PLYNG` character linked nowhere — the roomless-but-playing
state m-14 describes. This is the only −1-producing arm left in the function (T0b-1 finding
S10 / test ROW 1 commentary at `load_room_placement_tests.cpp:1709`).

### 2.2 The fix (owner ruling)

In `calc_load_room()`'s bugged arm: if `real_room(r_bugged_start_room)` is negative, fall back
to `r_mortal_start_room[GET_RACE(ch)]`. Bugged characters still go to the bugged room whenever
it resolves; the roomless-player class is retired at its only production source. The fix keeps
the arm's position (after the general clamp) and does not reorder any other arm.

### 2.3 Tests — red-first

- The two characterization tests that force this arm to −1 (`load_room_placement_tests.cpp:1770`
  and `:3203` region, via `r_bugged_start_room = 999999`) currently pin the linked-nowhere
  outcome. They flip to pin the racial-start outcome — same fixtures, same production call,
  inverted expectation, the wave's own flagship-witness pattern.
- A positive control pins that a *resolvable* bugged room is still used (bugged char lands in
  the bugged room, not racial start).
- ASan applies (test files touched).

### 2.4 Explicitly NOT touched

- `char_to_room`'s NOWHERE semantics (SETTLED, do-not-relitigate — O-5: NOWHERE means linked
  nowhere; changing the assignment would persist vnum 1101 and drop mortals into the immortal
  zone).
- The `act_obj2.cpp:726`/`:769` light-bump sites and the per-resolve mudlog: with the arm unable
  to return −1, the login path can no longer produce the state that reaches them; the general
  roomless case belongs to the operator[] fallback-retirement campaign.
- `r_bugged_start_room`'s misleading `r_` spelling: renaming it is cosmetic churn across tests
  and would widen the diff for zero behavior; a comment at the definition records that it holds
  a VNUM.

## 3. Verification

The standing cadence (AGENTS.local.md):

- Per-change: macOS-native build + `ctest --preset macos-arm64` + monolithic single-process run
  + six-seed shuffle + both censuses (`location_read_census.py --check`/`--self-test`,
  `string_view_census.py --check`) + boot golden; ASan preset (test files are touched);
  `rots64` leg + its boot golden.
- **`make smoke-account` is MANDATORY** — `calc_load_room()` is squarely on the login/rent path
  (AGENTS.md account/login rule + R-A2).
- Finalization, before merge: the full i386 battery (`scripts/i386-battery.sh`), all six
  blocking CI jobs green. Known traps: the Mach-O-in-bin restore
  (`cp /rots/build/ageland /rots/bin/ageland` in-container after a host smoke-account) and
  future-mtime normalization before the battery.
- Adversarial review before merge; merge is the owner's call.

Expected test-count movement: the `--self-test` growth is NOT a ctest count change
(`LocationReadCensusSelfTest` is one ctest test however many internal cases it drives — the
standing precedent); the m-14 work flips two existing tests in place and adds at least one
positive control, so the ctest total moves by +1 minimum. Goldens: no
boot-golden or seed42 drift is expected (the bugged arm is unreachable in the golden's world
data — vnum 1152 resolves there); any drift is a bug in the change.

## 4. Execution shape

Implementation is subagent-driven (Sonnet tier per the model-escalation gate: 0/4 criteria
tripped), two tasks:

1. **T1 — F-5**: ledger registry section + standing rule; census cross-check + sabotage-proven
   self-test cases; the `was_in_room` token promotion with census-annotation sweep.
2. **T2 — m-14**: red-first test flips + positive control, then the clamp.

Either order works; T1 first keeps the census gate at its widest before T2's annotations land.
