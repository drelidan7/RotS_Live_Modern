# RR Wave R2 — Small-Tier Classification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drain all 38 TODO rows (83 sites) in `src/entity`, `src/world`, `src/pathfind`, and
`src/olc` into `PROVEN`/`GUARDED` classifications, lower the ratchet 788 → the measured
remainder, and ship the classification playbook R3+ reuses.

**Architecture:** A read-only mini-census (T0) buckets every row by verified proof kind and
isolates the NOWHERE-reachable set; two classification tasks (T1: entity+pathfind+olc, T2:
world) edit ledger rows and land guards red-first; T3 writes the playbook from measured
actuals. The gate itself (`room_resolve_census.py --check`) is the per-commit acceptance test.

**Tech Stack:** the RR census tool + ledger (markdown), C++20/GoogleTest for guard tests,
CMake preset `macos-arm64` (+ ASan when tests land).

**Authority:** `docs/superpowers/specs/2026-08-01-rr2-small-tiers-design.md`; behind it,
`docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md`. The ledger doc's own
prose rules (proof discipline) are binding on every row edit.

## Global Constraints

- Branch `arch/rr2-small-tiers` (off master @`3e963a32`, 1862 tests). Never touch master.
- Scope: ONLY rows whose key file starts `src/entity/`, `src/world/`, `src/pathfind/`,
  `src/olc/`. Rows in combat/script/app are untouchable this wave, even via shared helpers.
- Every commit: all three censuses green (`room_resolve_census.py --check` AND `--self-test`,
  `location_read_census.py --check`, `string_view_census.py --check`), `ctest --preset
  macos-arm64` all-pass.
- Ratchet two-edit rule: `MAXIMUM_TODO_COUNT` (tools/room_resolve_census.py) and its self-test
  pin literal move together, in the SAME commit that drains the rows they account for.
- Proof discipline (binding, from the ledger prose): citations for entry-guard/
  caller-contract/dominating-resolve; BOTH halves for `location_of()` proofs (sentinel +
  in-range via M-1 `placement.cpp:369-395` + appends-only allocation); per-argument proofs
  for two-room macros; caller-contract on id-taking functions = enumerate every caller (a new
  token is a STOP, not an edit); mixed-class keys disambiguate by line.
- GUARDED = guard above the KEPT token; never a `room_by_id` rewrite. Red-first test per
  guard. Every guard enumerated in its commit message as a flagged behavior change.
- STOPs (return BLOCKED, do not improvise): a new-token candidate; an unclassifiable site; a
  site whose classification would require touching an out-of-scope row; any scanner or
  self-test change.
- Commit prefix `rr2:`; body trailer exactly:
Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012uev38h9HE1qzrNz1WHr2h

---

### Task 0: Mini-census (read-only)

**Files:**
- Create: `.superpowers/sdd/2026-08-01-rr2-small-tiers/rr2-census.md` (workspace artifact,
  git-ignored — the T1/T2 briefs consume it)

**Interfaces — Produces:** the census file, one section per tier, one entry per ledger row:

```markdown
## src/entity/equipment.cpp · attach_equipment · room_of(  (count 2)
- advisory: entry-guard? (line 214)
- verified kind: entry-guard  [or: NOWHERE-REACHABLE, or STOP + reason]
- proof draft: guarded at equipment.cpp:NNN `if (location_of(ch) == NOWHERE) return;`
  (cite the exact line and quote the guard); in-range: M-1 precondition
  (placement.cpp:369-395) + appends-only allocation
- sites: equipment.cpp:214, :229   [every site line for the key]
- [if NOWHERE-REACHABLE] absent-behavior proposal: <early return / skip / logged no-op>,
  test sketch: <name + what it pins, red-first shape>
- [flags] login-path? (smoke-account trigger) / two-room-macro? / mixed-class split needed?
```

- [ ] **Step 1:** Extract the 38 in-scope TODO rows: run
  `python3 -c` with the module's `parse_ledger` over `docs/superpowers/room-resolve-ledger.md`,
  filtering `cls == "TODO"` and the four path prefixes; cross-reference
  `python3 tools/room_resolve_census.py --advise` output for the same keys.
- [ ] **Step 2:** For each row, Read the enclosing function in full (not just the site lines)
  and verify or overturn the advisory kind. Rules of thumb: an occupant-chain walk
  (`occupants(`, `occupant_range(`, `ls_first_occupant_` via accessors) proves its member's
  location by the O-5 contrapositive + M-1 (state both); `for (... <= top_of_world ...)`
  is loop-bound; a `location_of(...) == NOWHERE` early-return above the site is entry-guard
  (cite it); a site fed by a caller's argument needs the caller enumerated (grep every caller;
  if any caller can pass NOWHERE, the row is NOWHERE-REACHABLE at THIS site, not the caller).
- [ ] **Step 3:** Write the census file; end with a summary block: counts per verified kind,
  the NOWHERE-REACHABLE list with proposals, expected test delta, smoke-account verdict
  (YES iff any guard touches login/rent-path code), and any STOPs.
- [ ] **Step 4:** No commit (workspace artifact only). Report DONE with the summary block
  inline.

### Task 1: Classify `entity` + `pathfind` + `olc` (25 rows / 48 sites)

**Files:**
- Modify: `docs/superpowers/room-resolve-ledger.md` (the 25 rows), `tools/room_resolve_census.py`
  (the two ceiling literals), plus any GUARDED production files + their test files (from T0).

**Interfaces — Consumes:** `rr2-census.md` entries for these tiers. **Produces:** ceiling at
788 − 48 = 740 (adjust to measured if any guard changes site counts — re-run `--check` and
use ITS arithmetic, never hand math).

- [ ] **Step 1 (per GUARDED site, if any — repeat):** write the red-first test from the T0
  sketch; run it (`ctest --preset macos-arm64 -R <TestName>` after build), quote the failure;
  implement the guard exactly as proposed; run green.
- [ ] **Step 2:** Edit the 25 ledger rows per the census file. Worked example of the row
  transformation shape (values from T0, not this example):

```markdown
| `src/entity/equipment.cpp · attach_equipment · room_of(` | 2 | TODO | — | — |
```
becomes
```markdown
| `src/entity/equipment.cpp · attach_equipment · room_of(` | 2 | PROVEN | entry-guard | guarded at src/entity/equipment.cpp:210 `if (location_of(ch) == NOWHERE) return;` — sentinel half; in-range half: M-1 placement precondition (placement.cpp:369-395) + appends-only allocation |
```

- [ ] **Step 3:** Lower `MAXIMUM_TODO_COUNT` 788 → 740 AND the self-test pin literal, same
  commit.
- [ ] **Step 4:** Gates: `python3 tools/room_resolve_census.py --check` (exit 0 — the
  reconciler is the acceptance test for your row edits) `&& --self-test`; both sibling
  censuses; `cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`
  (1862 + T0's measured test delta for these tiers); ASan preset iff a test file changed.
- [ ] **Step 5:** Commit (one commit; message lists every GUARDED site as a flagged behavior
  change, or states "all rows PROVEN, zero behavior change").

### Task 2: Classify `world` (13 rows / 35 sites)

Same shape as Task 1 exactly, consuming the census's `src/world` section. Ceiling 740 → 705
(adjust to measured). Known rows needing care (census verifies): the whole-world loops
(`reset_zone`, `recalc_zone_power` — loop-bound candidates), `show_tracks`/
`show_blood_trail` (entry-guard candidates per the advisory), `get_sun_level`
(occupant-loop candidate), and `db_world.cpp`'s remaining consumer rows (NOT the
RESOLVER-IMPL/PROVEN pinned ones — those are done).

- [ ] Steps 1-5 as Task 1, for these rows, with the T2 ceiling values.

### Task 3: Playbook + doc fold-in

**Files:**
- Create: `docs/superpowers/room-resolve-playbook.md`
- Modify: `AGENTS.md` (chain entry), `docs/BUILD.md` (one-sentence pointer next to the RR
  subsection)

- [ ] **Step 1:** Write the playbook from the wave's ACTUALS (the combat-migration-playbook
  pattern): per-proof-kind recipe sections each with one worked example row from this wave
  (quote the real row); the pitfall list (both-halves rule; advisory-suggestion overturn rate
  — measured from T0 vs final; caller-contract enumeration; two-room macros; mixed-class
  keys; the same-function site-swap limit reviewers audit for); the GUARDED procedure
  (red-first, absent-behavior statement, flagged-rider commit message); a per-row cost table:
  rows/sites per tier, wall-clock per task, fix-round count, test delta — seeded for R3
  (combat 95/186) and R4 (script 44/118) estimation.
- [ ] **Step 2:** AGENTS.md chain entry (measured numbers: rows drained, ceiling 788 → final,
  test delta, guards enumerated or "zero"); BUILD.md pointer sentence.
- [ ] **Step 3:** Gates (censuses + ctest — docs-only commit unless Step 2 counts moved) and
  commit.

### Task 4: Wave finalization

- [ ] **Step 1:** macOS legs: monolithic single-process from `src/tests`
  (`../../build/macos-arm64/ageland_tests`, expect exit 0; gtest-visible = final ctest total
  − 13 ctest-only) + six-seed shuffle (1/42/1234/98940/60928/777, `--gtest_repeat=3`) 0/0.
- [ ] **Step 2:** `rots64` container build+ctest+boot golden; native boot golden.
- [ ] **Step 3:** i386 battery (`scripts/i386-battery.sh`); smoke-account IFF T0 flagged a
  login-path guard (record the verdict either way).
- [ ] **Step 4:** Single whole-branch review (most capable tier) with the ledger's deferred
  minors; one fix wave + scoped re-review if findings; then push, PR (body: rows drained,
  ceiling movement, guards enumerated as flagged behavior changes, validation results), CI
  watch. Merge is the owner's call.

---

## Self-review record

- Spec §1 scope table → T1+T2 partition (25+13 = 38 ✓, 48+35 = 83 ✓); §2 workflow → Global
  Constraints + per-task steps; §3 structure → T0-T3 + finalization mapped 1:1; §4 movement →
  T1/T2 ceiling arithmetic + T3 measured numbers. The per-row proof content is T0's output by
  design — the plan supplies the process, formats, worked example, and STOPs.
- No placeholders: the worked example is labeled as shape-only; all exact values flow from
  T0's census file, which is a produced artifact, not a TBD.
