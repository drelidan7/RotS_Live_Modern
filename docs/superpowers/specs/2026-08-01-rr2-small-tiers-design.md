# RR Wave R2 — small-tier classification (entity / world / pathfind / olc)

**Date:** 2026-08-01
**Branch:** `arch/rr2-small-tiers` (off master @`3e963a32`, 1862 tests)
**Program authority:** `docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md`
(the dual-reviewed program spec; this brief applies its §3/§5 template — where they disagree,
the program spec wins). Owner-approved scope 2026-08-01: the four small library tiers.
**Review posture (owner-approved):** no dual spec review for this brief; per-task reviews +
one end-of-wave whole-branch review (most capable tier). Dual review returns at R-final.

## 1. Scope

Every TODO row under the four tiers, measured at `3e963a32`:

| Tier | Rows | Sites |
|---|---|---|
| `src/entity` | 16 | 31 |
| `src/world` | 13 | 35 |
| `src/pathfind` | 4 | 11 |
| `src/olc` | 5 | 6 |
| **Total** | **38** | **83** |

End state: zero TODO rows in these tiers; `MAXIMUM_TODO_COUNT` 788 → the measured remainder
(expected 705 = 788 − 83, minus any sites a guard's refactor removes — measured, not assumed),
lowered in the same commit that drains the rows (the pinned-literal two-edit discipline; the
self-test ceiling pin moves with it). `src/combat`, `src/script`, `src/app` rows are OUT of
scope — untouched even where a shared helper tempts a drive-by.

## 2. Workflow (the program spec's §3, applied)

- **Classification is per-row, evidence-first.** For each row: read the enclosing function,
  determine the site's input source, and either (a) write a `PROVEN` row with a
  vocabulary-kind proof — the advisory output pre-tags most rows (`entry-guard` clusters in
  `equipment.cpp`/`graph.cpp`/`db_world.cpp`; `occupant-loop`/`loop-bound` in `weather.cpp`/
  `zone.cpp`) but every suggestion is verified by reading, never accepted mechanically — or
  (b) rule the site NOWHERE-reachable and land a `GUARDED` conversion.
- **Proof discipline (from the ledger prose, binding):** `entry-guard`/`caller-contract`/
  `dominating-resolve` proofs cite `file:line`; every `location_of()`-based proof states BOTH
  halves (sentinel exclusion + in-range via the M-1 precondition and appends-only allocation);
  two-room macros (`IS_SUNLIT_EXIT`/`IS_SHADOWY_EXIT`) prove each room-id argument; a
  `caller-contract` proof on an id-taking function either pins the function name as a token
  (STOP — new tokens are a controller/owner decision, not an implementer edit) or enumerates
  every caller in the proof text; mixed-class keys disambiguate sites by line reference.
- **`GUARDED` conversions:** guard added above the KEPT token (never a `room_by_id` rewrite —
  program spec §2), the absent-behavior decided and stated in the row (early return, skip,
  logged no-op — whatever the surrounding contract wants), landed red-first with a named test
  per the standing coverage rule. Each guard is a real behavior change and is enumerated in
  the PR body (the O-2-precedent flagged-rider pattern, small scale).
- **Ratchet hygiene:** every commit leaves all three censuses green; TODO drains and ceiling
  drops travel together.

## 3. Structure

- **T0 — mini-census (read-only):** read all 38 rows' sites; bucket by proof kind; produce
  the NOWHERE-reachable set (the wave's only real unknown) with a per-site absent-behavior
  proposal; flag anything that needs a STOP (a new token candidate, an unclassifiable site, a
  login-path guard implying `make smoke-account` at finalization). Output: a census file in
  the wave workspace the T1/T2 briefs consume.
- **T1 — `entity` + `pathfind` + `olc`** (25 rows / 48 sites): classifications + any guards.
- **T2 — `world`** (13 rows / 35 sites): includes the two known whole-world walk loops
  (`reset_zone`, `recalc_zone_power`) and `show_tracks`/`show_blood_trail` — the relocated
  presentation functions LS waves moved into `db_world.cpp`.
- **T3 — the playbook + fold-in:** `docs/superpowers/room-resolve-playbook.md` — per-proof-kind
  recipes with worked examples from this wave, the pitfall list R1's reviews established, and
  a per-row cost table seeded from this wave's actuals (the combat-migration-playbook pattern)
  so R3 (combat, 95 rows) and R4 (script, 44 rows) plan against measured numbers. AGENTS.md
  chain entry + BUILD.md pointer.
- **Finalization:** standing cadence — macOS ctest/monolithic/six-seed; ASan iff a test file
  landed (guards imply tests, so expected YES — confirmed at T0); `rots64` + boot goldens;
  i386 battery; `make smoke-account` iff T0 flags a login-path guard; single whole-branch
  review (most capable tier); PR; merge is the owner's call.

## 4. Expected movement

Test count: 1862 + (one red-first test per GUARDED site — T0 measures; 0 if the tiers prove
fully PROVEN-able). Ledger: 38 TODO rows → 0 in scope; class counts and the token-counts
table updated; ceiling 788 → measured remainder. Zero behavior change outside the enumerated
GUARDED set. No new tokens, no scanner changes, no self-test changes expected — any of those
is a STOP back to the controller.
