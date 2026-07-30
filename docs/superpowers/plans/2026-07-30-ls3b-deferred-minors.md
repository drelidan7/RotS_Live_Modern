# LS-3b Deferred-MINORs Follow-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the three findings deferred from LS-3b's dual whole-branch reviews: F-5 (a
location-state registry cross-checked by the census gate), m-14 (clamp `calc_load_room()`'s
bugged arm so it can never return −1), and m-15 (skip, with a recorded disposition).

**Architecture:** Two independent work streams on branch `fix/ls3b-review-minors`: (1) the
census gate (`tools/location_read_census.py`) gains an eleventh token (`ls_load_room_vnum_`)
and a registry-consistency assertion in `--check`, backed by a new marker-anchored two-table
registry in `docs/superpowers/location-read-allowlist.md` and hermetic self-test coverage;
(2) `src/app/objsave.cpp`'s bugged-character arm gains a local clamp, driven red-first by
flipping two existing characterization tests onto a discrimination-safe fixture override.

**Tech Stack:** Python 3 (census script), C++20 + GoogleTest (game/tests), CMake preset
`macos-arm64` (+ `macos-arm64-asan`), markdown ledger docs.

**Authority:** `docs/superpowers/specs/2026-07-30-ls3b-deferred-minors-design.md` (the amended
spec, commit `d01b30d2`). Where this plan and the spec disagree, the spec wins.

## Global Constraints

- Branch: `fix/ls3b-review-minors`, baseline master @`0ee811ae` (1859 tests). Do not touch master.
- Every commit must leave the tree green: `ctest --preset macos-arm64` all-pass, both censuses
  (`python3 tools/location_read_census.py --check` and `--self-test`,
  `python3 tools/string_view_census.py --check`) exit 0.
- GNU builds are `-Wall -Wextra -Werror`; a warning is a build failure.
- All formatting: WebKit style via `cd src && make format` is the repo norm; match surrounding
  code (4-space indent, braces per surrounding file idiom).
- Tests: TDD, red-first. Every flipped or new assertion must be demonstrated failing against
  the pre-fix production code before the fix lands (evidence quoted in the commit message).
- ASan (`ctest --preset macos-arm64-asan`) required for any task that adds or rewrites a test
  file (Tasks 3).
- No new `LS1-ALLOW` reason prefixes — the authorized list stays at ten. The one new annotation
  uses `representation-decl`.
- The `--self-test` mode must stay hermetic: it may never read or mutate the real repository
  docs (its docstring guarantees it). Registry sabotage cases use synthetic fixture text only.
- Commit messages: imperative subject ≤72 chars, prefix `ls3b-minors:`.
- Boot goldens and the seed42 characterization golden must not drift at any commit (the login
  path is never exercised by either — any drift is a bug in your change).

---

### Task 1: `ls_load_room_vnum_` token promotion (census gate + one annotation + doc counts)

**Files:**
- Modify: `tools/location_read_census.py` (TOKEN_PATTERNS tuple, ~line 235; SELF_TEST_CASES
  tuple, ~line 649)
- Modify: `src/core/include/rots/core/character.h:353`
- Modify: `AGENTS.md` (the `tools/` bullet in "Project Structure & Module Organization" — the
  token count and the self-test case count)
- Modify: `docs/BUILD.md` (the token-count references — find them with
  `grep -n "ten" docs/BUILD.md | grep -i token`)

**Interfaces:**
- Produces: a `TOKEN_PATTERNS` entry named exactly `ls_load_room_vnum_` with pattern
  `re.compile(r"\bls_load_room_vnum_\b")`. Task 2's registry Table A row for
  `char_data::ls_load_room_vnum_` names this token, so the name string must match exactly.

- [ ] **Step 1: Write the failing self-test cases**

In `tools/location_read_census.py`, append to `SELF_TEST_CASES` (keep the existing tuple
formatting; each entry is `(name, body, expected_exit)`):

```python
    # LS-3b deferred-MINORs follow-up (spec review O-3/F-2): the fourth ls_*
    # private store gets the same bare-word token as its three siblings. The
    # ACCESSOR-GATED claim in the location-state registry is otherwise
    # enforced by nothing -- a raw write to the channel anywhere outside the
    # representation owner must be visible to this gate.
    ("ls-load-room-vnum-unannotated", "ch->specials.ls_load_room_vnum_ = 5;\n", 1),
    ("ls-load-room-vnum-annotated",
     "ch->specials.ls_load_room_vnum_ = 5; // LS1-ALLOW: write (probe)\n", 0),
```

- [ ] **Step 2: Run the self-test to verify it fails**

Run: `python3 tools/location_read_census.py --self-test`
Expected: FAIL — `ls-load-room-vnum-unannotated: expected gate exit 1, got 0` (the token does
not exist yet, so the probe line is invisible to the gate). The annotated case passes
trivially; that is fine — it becomes a real discriminator once the token exists.

- [ ] **Step 3: Add the token**

In `TOKEN_PATTERNS`, immediately after the `("ls_first_occupant_", ...)` entry and before the
token-paste comment block, insert:

```python
    # LS-3b deferred-MINORs follow-up (spec review O-3/F-2). The fourth ls_*
    # private store -- the login-window VNUM channel. Same bare-word design
    # and same rationale as the three rename tokens above: the spelling is
    # unique tree-wide, and a bare pattern also catches the declaration.
    # Promoted instead of `was_in_room` (the original draft's candidate):
    # this one costs exactly ONE annotation (the declaration; the only two
    # real access lines live in the whole-file-exempt representation owner,
    # placement.cpp), fits an existing prefix, and converts the registry's
    # ACCESSOR-GATED prose claim into a checked invariant.
    ("ls_load_room_vnum_", re.compile(r"\bls_load_room_vnum_\b")),
```

- [ ] **Step 4: Run --check to find the annotation debt**

Run: `python3 tools/location_read_census.py --check`
Expected: exit 1 with exactly ONE finding: `src/core/include/rots/core/character.h:353` (the
declaration). If more sites appear, STOP — the spec's one-annotation claim is falsified;
report to the controller instead of annotating ad hoc.

- [ ] **Step 5: Annotate the declaration**

In `src/core/include/rots/core/character.h:353`, extend the declaration line (match the
existing style of `ls_location_id_`'s annotation at `:862`):

```cpp
    int ls_load_room_vnum_ = NOWHERE; // LS1-ALLOW: representation-decl (the login-window VNUM channel's field itself -- not a call site; accessor-gated via stash_load_room_vnum/peek_load_room_vnum, placement.cpp)
```

(Keep any existing comment content on that line/block that documents the field's semantics —
append the annotation, do not delete prose.)

- [ ] **Step 6: Verify both census modes pass**

Run: `python3 tools/location_read_census.py --check && python3 tools/location_read_census.py --self-test`
Expected: `--check` exit 0 with `[scanned] N file(s)`; self-test all directions pass.

- [ ] **Step 7: Update the stale counts in AGENTS.md and BUILD.md**

- `AGENTS.md`, the `tools/` bullet: the sentence ending "…is **ten** — the five above plus…"
  gains: `; the LS-3b deferred-MINORs follow-up took it to **eleven** (a bare-word
  `ls_load_room_vnum_` token — the fourth ls_* private store, one annotation)`. Also update
  the `--self-test` case count there ("47 to **56**" chain) with the new measured count from
  Step 6's output — count the entries: `python3 -c "import importlib.util; s=importlib.util.spec_from_file_location('c','tools/location_read_census.py'); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); print(len(m.SELF_TEST_CASES))"`.
- `docs/BUILD.md`: `grep -n "TOKEN_PATTERNS" docs/BUILD.md` and update every count that says
  ten to eleven, with a one-line attribution to this follow-up PR.

- [ ] **Step 8: Build + full local test gate**

```bash
cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
```
Expected: all 1859 tests pass (no C++ behavior changed; the header edit is comment-only).

- [ ] **Step 9: Commit**

```bash
git add tools/location_read_census.py src/core/include/rots/core/character.h AGENTS.md docs/BUILD.md
git commit -m "ls3b-minors: promote ls_load_room_vnum_ to an eleventh census token"
```
Body: name the swap (was_in_room rejected per spec §1.2), the one-annotation measurement, and
the two new self-test cases.

---

### Task 2: The location-state registry + `--check` consistency assertion

**Files:**
- Modify: `docs/superpowers/location-read-allowlist.md` (new "Location-state registry" section;
  rewrite the "One named exclusion" paragraph found via `grep -n "One named exclusion" docs/superpowers/location-read-allowlist.md`;
  reconcile the `not-a-location` prose inventory found via `grep -n "was_in_room" docs/superpowers/location-read-allowlist.md`)
- Modify: `tools/location_read_census.py` (new constants + `parse_registry` +
  `check_registry_consistency` + `main()` wiring + `run_self_test` additions + module
  docstring reconciliation)
- Modify: `AGENTS.md` / `docs/BUILD.md` (self-test case count again; one sentence in BUILD.md's
  "Library layering" location note pointing at the registry)

**Interfaces:**
- Consumes: Task 1's token name `ls_load_room_vnum_`.
- Produces: `parse_registry(text: str) -> tuple[list[dict], list[dict]]` (Table A rows, Table B
  rows; each row dict has keys `store` (full backticked spelling), `member` (last component),
  `coverage_tokens` (list of backticked names from the Coverage cell, possibly empty)) and
  `check_registry_consistency(rows_a, rows_b) -> list[str]` (empty list = consistent). Both
  called from `main()` under `--check`.

- [ ] **Step 1: Add the registry section to the ledger**

Append to `docs/superpowers/location-read-allowlist.md` (new top-level section at the end of
the doc). The two markers must appear exactly once each, outside any fenced code block. Copy
this content verbatim (it is the spec's §1.1, in parser-friendly form):

```markdown
## Location-state registry

The declared closed world of room-id storage (spec:
`docs/superpowers/specs/2026-07-30-ls3b-deferred-minors-design.md` §1). Any future storage --
struct member, file-scope global, or persisted field -- that carries a room id (rnum, vnum, or
handle) MUST be added here in the same commit that introduces it: Table A with a coverage
disposition if it records an entity's whereabouts, Table B with a class if not. A TOKEN
disposition additionally requires the matching `TOKEN_PATTERNS` entry in
`tools/location_read_census.py`; `--check` fails closed if the two drift one-sidedly.

Mechanical detection of an arbitrarily-named new store is impossible (LS-3b review-2 F-5's own
finding); this registry makes "add a store without registering it" a reviewable process
violation rather than a silent gap. The check is closed against ONE-SIDED drift only: a commit
that deletes a token and downgrades the matching row in the same edit passes both directions --
a coordinated two-sided edit is a review question, not a gate question.

The compile-time absence assertion (`src/entity/placement.cpp`) covers NONE of the rows below:
it witnesses only the absence of the three retired public spellings
(`in_room`/`next_in_room`/`people`). The retired-spelling guard tokens (`next_in_room`,
`people`) are exempt from the reverse check below; their validity condition is that no struct
anywhere spells those members -- a future struct reusing a retired spelling fires the token
per-line but must be re-adjudicated against this registry.

### Table A -- entity location stores

<!-- LOCATION-STATE-REGISTRY-TABLE-A -->
| Store | Declared at | Kind | Repr | Coverage |
| --- | --- | --- | --- | --- |
| `char_data::ls_location_id_` | `core/character.h:862` | live (private handle) | rnum | TOKEN `ls_location_id_` |
| `char_data::ls_next_in_room_` | `core/character.h:898` | live (private chain link) | handle | TOKEN `ls_next_in_room_` |
| `room_data::ls_first_occupant_` | `core/room.h:126` | live (private chain head) | handle | TOKEN `ls_first_occupant_` |
| `char_data::specials.ls_load_room_vnum_` | `core/character.h:353` | live (login-window channel; accessor-gated via `stash_load_room_vnum`/`peek_load_room_vnum`) | vnum | TOKEN `ls_load_room_vnum_` |
| `char_data::specials.was_in_room` | `core/character.h:340` | live (linkdead stash; NOT the representation, per R-C5 above) | rnum | UNTRACKED-BY-DESIGN (tokening it would mint ~15 permanent annotations with no honest reason prefix and no burndown path -- spec review O-3/F-3) |
| `char_special2_data::load_room` | `core/types.h` | PERSISTED (playerfile; rnum transiently in the equip_lost window) | vnum | UNTRACKED-BY-DESIGN (pervasive; a token needs its own census) |
| `affected_type::modifier` | `core/types.h:719` | PERSISTED (playerfile; a room rnum under `SPELL_BEACON` only) | rnum | UNTRACKED-BY-DESIGN (generic field name -- `obj_affected_type::modifier` at `:384` is a second member of the same spelling; guarded by the O-7 two-sided load/save guard instead) |
| `obj_data::in_room` | `core/object.h:165` | deferred (LS-4 campaign) | rnum | TOKEN `->in_room` `.in_room` `::in_room` |
| `shop_data::in_room` | `app/shop.cpp:63` | not-a-location (shop identity) | vnum | TOKEN `->in_room` `.in_room` `::in_room` |

### Table B -- other room-id carriers

<!-- LOCATION-STATE-REGISTRY-TABLE-B -->
| Carrier | Declared at | Repr | Class |
| --- | --- | --- | --- |
| `room_direction_data::to_room` | `core/types.h:395` | rnum | world topology (exit target; the LS-2 review's dangling-fixture-pointer class) |
| `shop_data::stock_room` | `app/shop.cpp:64` | vnum | object-parameter room reference (LS-2 T3d resolver trap) |
| `obj_data::obj_flags.value[0]` | `core/object.h` (meaning under `ITEM_LEVER` only) | vnum | object-parameter room reference (resolved at `act_move.cpp:1829`; included because `SPELL_BEACON` is -- same discriminated-generic-field class) |
| `target_data::ptr.room` | `core/types.h:248` | handle | transient targeting (queued-command target) |
| `r_mortal_start_room[]` / `r_mortal_idle_room[]` / `r_immort_start_room` / `r_frozen_start_room` / `r_retirement_home_room` / `mortal_maze_room[][2]` | `core/consts.cpp` (recomputed by `check_start_rooms()`, `world/db_world.cpp`) | rnum | boot/world configuration |
| `r_bugged_start_room` | `core/consts.cpp:2554` | **vnum, despite the `r_` spelling** (never boot-recomputed; the m-14 root cause -- see this PR's spec §2) | boot/world configuration |
```

- [ ] **Step 2: Write the failing self-test additions**

In `run_self_test()`, after the existing `src/tests`-scope probes and before the closing
`for failure in failures:` loop, add (this drives functions that do not exist yet — the
self-test will crash, which is the red state):

```python
        # ------------------------------------------------------------------
        # Location-state registry cross-check (deferred-MINORs follow-up,
        # spec 1.3). All synthetic: the real ledger is never read or mutated
        # here -- --check owns the real-doc assertion.
        # ------------------------------------------------------------------
        rows_a, rows_b = parse_registry(SELF_TEST_REGISTRY_OK)
        errors = check_registry_consistency(rows_a, rows_b)
        if errors:
            failures.append(f"registry: the known-good synthetic registry failed: {errors}")

        # Sabotage (a): a TOKEN row naming a token that does not exist.
        bad_a, bad_b = parse_registry(
            SELF_TEST_REGISTRY_OK.replace("TOKEN `ls_location_id_`",
                                          "TOKEN `no_such_token_`"))
        if not check_registry_consistency(bad_a, bad_b):
            failures.append("registry: a row naming a nonexistent token was not caught")

        # Sabotage (b): a non-exempt token with no registry row. Drop the
        # ls_load_room_vnum_ row entirely.
        dropped = "\n".join(line for line in SELF_TEST_REGISTRY_OK.splitlines()
                            if "ls_load_room_vnum_" not in line)
        drop_a, drop_b = parse_registry(dropped)
        if not check_registry_consistency(drop_a, drop_b):
            failures.append("registry: a rowless non-exempt token was not caught")

        # Sabotage (c): registry missing / unparseable -- parse_registry must
        # raise, not return empty (fail closed).
        for broken in ("no tables at all",
                       SELF_TEST_REGISTRY_OK.replace(
                           "<!-- LOCATION-STATE-REGISTRY-TABLE-A -->", "")):
            try:
                parse_registry(broken)
                failures.append("registry: a missing/unmarked table parsed as success")
            except SystemExit:
                pass

        # Sabotage (c'): below the row floor -- Table A truncated to one row.
        header_end = SELF_TEST_REGISTRY_OK.index("| `char_data::ls_next_in_room_`")
        try:
            parse_registry(SELF_TEST_REGISTRY_OK[:header_end])
            failures.append("registry: a below-floor Table A parsed as success")
        except SystemExit:
            pass

        # Accessor-anchored coverage must satisfy check 1 via probe synthesis
        # (spec review O-5/F-5): the known-good registry's obj_data::in_room
        # row is the standing witness -- it is covered by the three accessor
        # patterns, none of which matches the bare member spelling.
```

And add the synthetic fixture constant next to `SELF_TEST_LEDGER` (it must mirror the REAL
Table A's coverage of every non-exempt token, because `check_registry_consistency` checks
against the real `TOKEN_PATTERNS`):

```python
# A synthetic, known-good registry mirroring the real one's token coverage.
# Used only by --self-test; the real registry lives in the ledger doc and is
# asserted by --check. If a future wave adds a token, this fixture needs the
# matching row too -- the self-test failing here is the reminder.
SELF_TEST_REGISTRY_OK = """
<!-- LOCATION-STATE-REGISTRY-TABLE-A -->
| Store | Declared at | Kind | Repr | Coverage |
| --- | --- | --- | --- | --- |
| `char_data::ls_location_id_` | `core/character.h:862` | live | rnum | TOKEN `ls_location_id_` |
| `char_data::ls_next_in_room_` | `core/character.h:898` | live | handle | TOKEN `ls_next_in_room_` |
| `room_data::ls_first_occupant_` | `core/room.h:126` | live | handle | TOKEN `ls_first_occupant_` |
| `char_data::specials.ls_load_room_vnum_` | `core/character.h:353` | live | vnum | TOKEN `ls_load_room_vnum_` |
| `char_data::specials.was_in_room` | `core/character.h:340` | live | rnum | UNTRACKED-BY-DESIGN (R-C5) |
| `char_special2_data::load_room` | `core/types.h` | PERSISTED | vnum | UNTRACKED-BY-DESIGN (pervasive) |
| `affected_type::modifier` | `core/types.h:719` | PERSISTED | rnum | UNTRACKED-BY-DESIGN (generic name) |
| `obj_data::in_room` | `core/object.h:165` | deferred | rnum | TOKEN `->in_room` `.in_room` `::in_room` |
| `shop_data::in_room` | `app/shop.cpp:63` | not-a-location | vnum | TOKEN `->in_room` `.in_room` `::in_room` |

<!-- LOCATION-STATE-REGISTRY-TABLE-B -->
| Carrier | Declared at | Repr | Class |
| --- | --- | --- | --- |
| `room_direction_data::to_room` | `core/types.h:395` | rnum | world topology |
| `shop_data::stock_room` | `app/shop.cpp:64` | vnum | object-parameter |
| `obj_data::obj_flags.value[0]` | `core/object.h` | vnum | object-parameter |
| `target_data::ptr.room` | `core/types.h:248` | handle | transient targeting |
| `r_mortal_start_room[]` etc. | `core/consts.cpp` | rnum | boot configuration |
| `r_bugged_start_room` | `core/consts.cpp:2554` | vnum | boot configuration |
"""
```

- [ ] **Step 3: Run the self-test to verify it fails**

Run: `python3 tools/location_read_census.py --self-test`
Expected: `NameError: name 'parse_registry' is not defined` (red state).

- [ ] **Step 4: Implement `parse_registry` and `check_registry_consistency`**

Add below `load_allow_listed_files` in `tools/location_read_census.py`:

```python
# The registry's two tables are marker-anchored for the same reason the
# allow-list table is (Opus M10: a look-alike table elsewhere in the doc must
# be inert). Floors are pinned per table so the check can never pass by
# finding an empty or truncated registry.
REGISTRY_TABLE_A_MARKER = "<!-- LOCATION-STATE-REGISTRY-TABLE-A -->"
REGISTRY_TABLE_A_HEADER = "| Store | Declared at | Kind | Repr | Coverage |"
REGISTRY_TABLE_B_MARKER = "<!-- LOCATION-STATE-REGISTRY-TABLE-B -->"
REGISTRY_TABLE_B_HEADER = "| Carrier | Declared at | Repr | Class |"
MINIMUM_REGISTRY_ROWS_A = 9
MINIMUM_REGISTRY_ROWS_B = 6

# Tokens that legitimately map to no Table-A row. Structural tokens track
# syntax, not stores; the retired-spelling guards track member names no
# current store spells (kept to catch reintroduction). Validity condition
# recorded in the ledger beside the registry.
REGISTRY_EXEMPT_TOKENS = frozenset({
    "world[",
    "## (preprocessor token paste)",
    "next_in_room",
    "people",
})


def _strip_fences(text):
    """Drop fenced code blocks -- documentation, never data (M10 precedent)."""
    live_lines = []
    in_fence = False
    for line in text.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            live_lines.append(line)
    return live_lines


def _parse_marked_table(live_lines, marker, header, minimum_rows, label):
    """Parse the rows of the single marker-anchored table. Fails closed."""
    marker_count = sum(1 for line in live_lines if line.strip() == marker)
    if marker_count != 1:
        raise SystemExit(
            f"error: registry: {label} marker {marker!r} must appear exactly once "
            f"outside any fenced code block (found {marker_count})."
        )
    rows = []
    state = "before-marker"
    for line in live_lines:
        stripped = line.strip()
        if state == "before-marker":
            if stripped == marker:
                state = "awaiting-header"
            continue
        if state == "awaiting-header":
            if not stripped:
                continue
            if stripped != header:
                raise SystemExit(
                    f"error: registry: the line after {marker!r} must be "
                    f"{header!r}, got {stripped!r}."
                )
            state = "in-table"
            continue
        if stripped.startswith("|") and set(stripped) <= set("|- :"):
            continue
        if not stripped.startswith("|"):
            break
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        first_cell_match = re.match(r"^`([^`]+)`", cells[0])
        if first_cell_match is None:
            continue
        spelling = first_cell_match.group(1)
        member = spelling.rsplit("::", 1)[-1].rsplit(".", 1)[-1]
        coverage_tokens = re.findall(r"`([^`]+)`", cells[-1]) if cells[-1].startswith("TOKEN") else []
        rows.append({"store": spelling, "member": member, "coverage_tokens": coverage_tokens})
    if len(rows) < minimum_rows:
        raise SystemExit(
            f"error: registry: {label} has {len(rows)} rows, below the floor of "
            f"{minimum_rows} -- a truncated registry must not pass."
        )
    return rows


def parse_registry(text):
    """Parse the location-state registry (Tables A and B) out of ledger text."""
    live_lines = _strip_fences(text)
    rows_a = _parse_marked_table(live_lines, REGISTRY_TABLE_A_MARKER,
                                 REGISTRY_TABLE_A_HEADER, MINIMUM_REGISTRY_ROWS_A, "Table A")
    rows_b = _parse_marked_table(live_lines, REGISTRY_TABLE_B_MARKER,
                                 REGISTRY_TABLE_B_HEADER, MINIMUM_REGISTRY_ROWS_B, "Table B")
    return rows_a, rows_b


def check_registry_consistency(rows_a, rows_b):
    """The bidirectional closed-world cross-check (spec 1.3). Returns errors."""
    del rows_b  # Table B is floor-checked by the parser; it carries no tokens.
    errors = []
    patterns_by_name = dict(TOKEN_PATTERNS)

    # Direction 1: every TOKEN row names real tokens whose pattern matches a
    # synthesized probe for the member. Accessor-anchored patterns cannot
    # match a bare spelling (the anchor is their point -- spec review O-5), so
    # probe with each access shape and accept any hit.
    for row in rows_a:
        for token_name in row["coverage_tokens"]:
            pattern = patterns_by_name.get(token_name)
            if pattern is None:
                errors.append(
                    f"registry row `{row['store']}` names token `{token_name}`, "
                    f"which is not in TOKEN_PATTERNS"
                )
                continue
            member = row["member"]
            probes = (member, f"x->{member}", f"x.{member}", f"char_data::{member}")
            if not any(pattern.search(probe) for probe in probes):
                errors.append(
                    f"registry row `{row['store']}`: token `{token_name}`'s pattern "
                    f"matches no synthesized probe for member `{member}`"
                )

    # Direction 2: every non-exempt token maps to at least one Table-A row.
    covered = {name for row in rows_a for name in row["coverage_tokens"]}
    for token_name, _ in TOKEN_PATTERNS:
        if token_name in REGISTRY_EXEMPT_TOKENS:
            continue
        if token_name not in covered:
            errors.append(
                f"token `{token_name}` has no location-state registry row and no "
                f"exemption -- register the store or exempt the token, in the ledger"
            )
    return errors
```

- [ ] **Step 5: Run the self-test to verify it passes**

Run: `python3 tools/location_read_census.py --self-test`
Expected: all directions pass, including the five new registry directions.

- [ ] **Step 6: Wire the real-doc assertion into `--check`**

**Gating fact (verified while writing this plan):** `_run_gate` (~line 837) invokes the real
`main()` with `--check` AND a synthetic `--exceptions` ledger that contains no registry. So the
assertion must NOT be gated on `arguments.check` alone — that would make every existing
self-test case die in `parse_registry`'s fail-closed SystemExit. Gate it on the real default
ledger instead:

In `main()`, after `allow_listed_files = load_allow_listed_files(...)` add:

```python
    # The location-state registry consistency assertion (spec 1.3) runs in
    # --check against the REAL ledger only. A caller supplying --exceptions
    # (the hermetic self-test's synthetic ledgers, which carry no registry)
    # is probing the token gate, not the registry -- the registry's own
    # failure directions are standing synthetic cases in run_self_test().
    if arguments.check and arguments.exceptions is None:
        registry_rows_a, registry_rows_b = parse_registry(
            exception_path.read_text(encoding="utf-8"))
        registry_errors = check_registry_consistency(registry_rows_a, registry_rows_b)
        if registry_errors:
            for error in registry_errors:
                print(f"registry inconsistency: {error}", file=sys.stderr)
            return 1
```

(If `main()`'s `--check` failure path uses a different exit idiom, match it.) Add one standing
self-test assertion documenting the gating: a registry-less synthetic ledger must still gate
token findings (this already holds — every existing case proves it — so add a one-line comment
beside the new registry cases naming that property rather than a redundant probe).

- [ ] **Step 7: Run both census modes + one deliberate real-doc sabotage (one-time, non-vacuity)**

```bash
python3 tools/location_read_census.py --check && python3 tools/location_read_census.py --self-test
```
Expected: both exit 0. Then the one-time real-doc demonstration (in addition to, never instead
of, the standing cases): temporarily change `TOKEN \`ls_location_id_\`` to
`TOKEN \`no_such_token_\`` in the ledger, run `--check`, expect exit 1 with the
`registry inconsistency:` line, then `git checkout docs/superpowers/location-read-allowlist.md`
and re-run `--check` to confirm exit 0. Quote the sabotage output in the commit message.

- [ ] **Step 8: Reconcile the standing prose**

- In `docs/superpowers/location-read-allowlist.md`: find the "One named exclusion remains
  untracked by construction" paragraph and rewrite its claim to cite Table A (`was_in_room`'s
  row is now the authoritative record of WHY it is untracked); update the `not-a-location`
  prose inventory sentence that lists `was_in_room` as untracked to point at the registry.
- In `tools/location_read_census.py`'s module docstring: update the untracked-exclusion
  narrative (grep `was_in_room` in the docstring region, ~lines 16-27) to mention the registry
  as the closed-world record.
- In `docs/BUILD.md`'s "Library layering" location note: one sentence — the ledger now carries
  a marker-anchored location-state registry cross-checked by `--check`; new room-id storage
  must register (cite the spec).
- `AGENTS.md`: update the self-test case count again if it moved in this task.

- [ ] **Step 9: Full local gate + commit**

```bash
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
python3 tools/string_view_census.py --check
git add tools/location_read_census.py docs/superpowers/location-read-allowlist.md AGENTS.md docs/BUILD.md
git commit -m "ls3b-minors: location-state registry + --check consistency assertion (F-5)"
```

---

### Task 3: m-14 — red-first flips, positive control, the clamp

**Files:**
- Modify: `src/tests/load_room_placement_tests.cpp` (the two `LoadRoomRider` bugged-arm tests,
  ~lines 1705-1845 and ~3160-3255; plus one new test)
- Modify: `src/app/objsave.cpp` (`calc_load_room`, the bugged arm at ~line 621-622; the
  channel-lifetime comment block at ~517-540)
- Modify: `src/app/interpre.cpp` (~lines 3801-3811, the rationale comment)
- Modify: `src/core/consts.cpp:2554` (comment only)

**Interfaces:**
- Consumes: nothing from Tasks 1-2 (fully independent).
- Produces: nothing consumed later. Test names:
  `LoadRoomRider.PostLoginSaveLandsABuggedCharacterInTheRacialStartRoom` (flipped ROW-1),
  `LoadRoomRider.PostLoginSaveOfABuggedCharacterPersistsNowhereThroughTheRealLoadCharacter`
  (kept name, flipped placement assertions), `LoadRoomRider.ResolvableBuggedRoomIsStillUsed`
  (new positive control).

**Fixture facts** (verify at head of task; all in `load_room_placement_tests.cpp`):
`ScopedVnumWorld` builds 6 rooms, rnums 0-5, vnums `room_vnum_for(rnum) = 5 + 10*rnum`
(5,15,25,35,45,55). `ScopedStartRooms` pins every `r_mortal_start_room[race]` (and immort/
frozen) to `kRacialStartRnum = 0` and `r_bugged_start_room = room_vnum_for(0) = 5`, and
restores all of them in its destructor — so per-test overrides after construction are safe.
**The discrimination trap (spec §2.3):** rnum 0 is ALSO what the pre-fix room-0 fallback
resolves to, so every flipped assertion must use a start rnum ≠ 0. Use rnum **1** (vnum 15)
for the racial start override and rnum **2** (vnum 25) for the positive control's bugged room.

- [ ] **Step 1: Flip ROW-1 (rename + rewrite), red-first**

Rewrite `LoadRoomRider.PostLoginSaveLeavesABuggedCharacterNowhereAndLinksThemIntoNoRoomAtAll`
(~line 1744). Keep the fixture scaffolding (descriptor, name, `r_bugged_start_room = 999999`,
bugged stats, `load_room = kOwnerVnum`, `replay_load_character_guard`) exactly as is; change
the name, add the start-room override, and flip the placement assertions. The GET_LOADROOM
block is UNCHANGED — the persisted-NOWHERE property survives the clamp by design and must keep
its witness:

```cpp
// ROW 1, post-m-14: the bugged arm can no longer return -1. When
// real_room(r_bugged_start_room) misses, calc_load_room() clamps to the
// racial start room, so the character is genuinely PLACED -- and save_char's
// persisted value stays NOWHERE (the interpre.cpp rationale's routing), which
// is the half of the old test that does not flip.
//
// DISCRIMINATION (spec review O-2): the racial start room is overridden to
// rnum 1 because the fixture default (rnum 0) is the same room the pre-fix
// room-0 fallback resolves to -- assertions against rnum 0 cannot tell the
// clamp from the bug. RED-FIRST: against the unclamped arm, computed here is
// -1 and location_of() stays NOWHERE, so the first two assertions fail.
TEST(LoadRoomRider, PostLoginSaveLandsABuggedCharacterInTheRacialStartRoom) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    // The discrimination override: a start room distinct from rnum 0.
    constexpr int kDistinctStartRnum = 1;
    for (int race = 0; race < MAX_RACES; ++race)
        r_mortal_start_room[race] = kDistinctStartRnum;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("buggedchr") + 1);
    strcpy(player.player.name, "buggedchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    // A vnum no room in this fixture carries, so real_room() misses and the
    // bugged arm takes its NEW fallback.
    r_bugged_start_room = 999999;
    player.abilities.str = 0;
    player.tmpabilities.str = 0;

    player.specials2.load_room = kOwnerVnum;
    replay_load_character_guard(player);
    const int computed_load_room = calc_load_room(&player, RENT_RENTED);
    ASSERT_EQ(computed_load_room, kDistinctStartRnum)
        << "the clamped bugged arm must fall back to the racial start room";

    char_to_room(&player, computed_load_room);
    ASSERT_EQ(location_of(&player), kDistinctStartRnum);
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(kDistinctStartRnum)), &player)
        << "a placed character must be linked into the start room's chain";
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(0)), nullptr)
        << "...and NOT into room 0 (the pre-fix fallback room)";
    stash_load_room_vnum(&player, NOWHERE);

    player.specials2.load_room = -12345;
    save_char(&player, NOWHERE, 0);

    // UNCHANGED from the pre-flip test: the persisted value. interpre.cpp
    // passes NOWHERE deliberately and the channel is retired, so save_char
    // persists NOWHERE whether or not placement succeeded. This is the B-1
    // property, and it keeps its witness here.
    EXPECT_EQ(GET_LOADROOM(&player), NOWHERE);
    EXPECT_NE(GET_LOADROOM(&player), kOwnerVnum);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    EXPECT_NE(GET_LOADROOM(&player), room_vnum_for(0));

    EXPECT_EQ(room_by_id_total(location_of(&player))->number,
              room_vnum_for(kDistinctStartRnum));

    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "output: " << descriptor.small_outbuf;

    // LOAD-BEARING AGAIN (spec review O-2, the DoRescue/waiting_list class):
    // the character above is a stack object spliced into a process-global
    // chain. Unlink from the room it actually landed in, or the pointer
    // outlives this frame -- ctest cannot see that; the monolithic runner
    // and the i386 battery can.
    unlink_from_occupant_chain(*room_by_id_total(kDistinctStartRnum), &player);
    RELEASE(player.player.name);
}
```

- [ ] **Step 2: Flip the B-1a witness's placement half, red-first**

In `LoadRoomRider.PostLoginSaveOfABuggedCharacterPersistsNowhereThroughTheRealLoadCharacter`
(~line 3175): add the same `kDistinctStartRnum = 1` override right after the fixtures; replace
the two placement assertions after `load_character(&player)`:

```cpp
    ASSERT_EQ(location_of(&player), 1)
        << "the clamped bugged arm must place the character in the racial start room";
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(1)), &player);
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(0)), nullptr)
        << "...and NOT room 0 (the pre-fix fallback)";
```

Keep every `GET_LOADROOM` assertion byte-identical (the B-1 discriminator). Before
`RELEASE(player.player.name);` at the end, add the now-required unlink:

```cpp
    unlink_from_occupant_chain(*room_by_id_total(1), &player);
```

Update the test's leading comment block: the bugged arm no longer returns −1 (cite the spec);
the RED-FIRST paragraph gets a second clause — see Step 6.

- [ ] **Step 3: Add the positive control (vacuity-proofed)**

New test, immediately after the flipped ROW-1 test:

```cpp
// The positive control (spec 2.3): a RESOLVABLE bugged room is still used --
// the clamp must not overshoot into always-racial-start. Vacuity-proofed
// (the O-I3 class): the bugged room (rnum 2) is distinct from BOTH the
// overridden racial start (rnum 1) and room 0, so this fails if the clamp
// ignores a resolvable bugged room, and fails differently if the arm still
// returns -1.
TEST(LoadRoomRider, ResolvableBuggedRoomIsStillUsed) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    for (int race = 0; race < MAX_RACES; ++race)
        r_mortal_start_room[race] = 1;
    r_bugged_start_room = room_vnum_for(2); // vnum 25 -> rnum 2, resolvable

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // Below the bugged-character floor (str >= 1).
    player.abilities.str = 0;
    player.tmpabilities.str = 0;
    player.specials2.load_room = kOwnerVnum;
    replay_load_character_guard(player);

    EXPECT_EQ(calc_load_room(&player, RENT_RENTED), 2)
        << "a resolvable bugged room must win over the racial-start fallback";
}
```

- [ ] **Step 4: Run the three tests to verify they fail (red-first evidence)**

```bash
cd src && cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R LoadRoomRider --output-on-failure
```
Expected: `PostLoginSaveLandsABuggedCharacterInTheRacialStartRoom` FAILS at
`computed_load_room == 1` (actual −1); `PostLoginSaveOfABuggedCharacterPersistsNowhere…` FAILS
at `location_of == 1` (actual NOWHERE); `ResolvableBuggedRoomIsStillUsed` PASSES already (the
unclamped arm resolves rnum 2 fine) — it is red against the *overshoot* mutation, verified in
Step 7. Save the failure output for the commit message.

- [ ] **Step 5: Implement the clamp**

In `src/app/objsave.cpp`, replace the bugged arm (~line 621-622):

```cpp
    /* here checking for bugged characters */
    if (ch->tmpabilities.str < 1 || ch->abilities.str < 1 || ch->tmpabilities.dex < 1 || ch->abilities.dex < 1 || ch->tmpabilities.move < 1 || ch->points.spirit < 0 || ch->points.spirit > 100000 || ch->tmpabilities.move > 1000) {
        // m-14 (LS-3b review follow-up): r_bugged_start_room holds a VNUM
        // despite its r_ spelling and is never boot-recomputed, and this arm
        // sits AFTER the general `< 0` clamp above -- so a world without that
        // vnum made this the function's only -1 return, sending a CON_PLYNG
        // character into char_to_room(ch, NOWHERE). Fall back to the racial
        // start room instead. Deliberately a LOCAL clamp, not a relocation of
        // the general one: moving that would silently change what any future
        // arm added below it inherits.
        load_room = real_room(r_bugged_start_room);
        if (load_room < 0)
            load_room = r_mortal_start_room[GET_RACE(ch)];
    }
```

And in `src/core/consts.cpp:2554`:

```cpp
// Holds a VNUM despite the r_ prefix (it is never boot-recomputed by
// check_start_rooms); calc_load_room() resolves it through real_room() and
// clamps to the racial start room if it does not resolve (m-14).
int r_bugged_start_room = 1152;
```

- [ ] **Step 6: Update the counterfactual rationale comments (spec §2.4)**

- `src/app/interpre.cpp` ~3801-3811: rewrite the "calc_load_room() can return -1" sentence —
  the arm now clamps (cite `objsave.cpp`'s bugged arm by function, not line number, since the
  old `:588-589` citation was already stale); keep the surrounding rationale about passing
  NOWHERE deliberately (it is still correct and still load-bearing for the B-1 witness).
- `src/app/objsave.cpp` ~517-540 (the channel-lifetime block): update the parenthetical "(its
  bugged arm can return -1)" to past tense with a pointer at the clamp.
- The flipped tests' comment blocks (done inline in Steps 1-2).

- [ ] **Step 7: Run tests green + two sabotage probes (positive-control non-vacuity, B-1 red-first re-demonstration)**

```bash
cd src && cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R LoadRoomRider --output-on-failure
```
Expected: all pass. Then two one-time probes (quote both in the commit message):
1. **Overshoot mutation:** temporarily change the clamp to unconditionally
   `load_room = r_mortal_start_room[GET_RACE(ch)];` (ignoring the resolvable bugged room),
   rebuild, run — `ResolvableBuggedRoomIsStillUsed` must FAIL (2 vs 1). Revert.
2. **B-1 witness re-demonstration:** temporarily comment out the channel-retirement statement
   in `load_character()` (`stash_load_room_vnum(ch, NOWHERE);`, `objsave.cpp` ~546), rebuild,
   run — `PostLoginSaveOfABuggedCharacterPersistsNowhere…` must FAIL on
   `GET_LOADROOM == NOWHERE` (actual `kOwnerVnum`, 35). Revert. This proves the flip preserved
   the T9b BLOCKER B-1 discriminator.

- [ ] **Step 8: Full suite + ASan + censuses + boot golden**

```bash
cd src && ctest --preset macos-arm64
cmake --preset macos-arm64-asan && cmake --build --preset macos-arm64-asan -j4 && ctest --preset macos-arm64-asan
cd .. && python3 tools/location_read_census.py --check && python3 tools/string_view_census.py --check
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
```
Expected: 1860/1860 both presets (+1 from the positive control), censuses 0, boot golden
matches byte-identical.

- [ ] **Step 9: Commit**

```bash
git add src/app/objsave.cpp src/app/interpre.cpp src/core/consts.cpp src/tests/load_room_placement_tests.cpp
git commit -m "ls3b-minors: clamp calc_load_room's bugged arm to racial start (m-14)"
```
Body: the red-first evidence from Step 4, both sabotage-probe outputs from Step 7, and the
owner ruling + accepted asymmetry (spec §2.2).

---

### Task 4: Branch finalization — cross-host gates, records, PR

**Files:**
- Modify: `AGENTS.md` (fold the follow-up's numbers into the LS-3b chain entry, the PR #23/#27
  precedent: +1 test → 1860, the eleventh token, the registry)
- No other source changes.

- [ ] **Step 1: Monolithic single-process run + six-seed shuffle (macOS)**

```bash
cd src/tests && make clean && make tests && ../../bin/tests; cd ../..
```
Expected: exit 0, no SIGSEGV — this is the gate that sees the Step-1/2 unlink obligations. Then
the standing six-seed shuffle (`--gtest_shuffle --gtest_repeat=3`, seeds 1/42/1234/98940/
60928/777) on the ctest binary: 0 crashes / 0 failures each.

- [ ] **Step 2: `rots64` leg + its boot golden**

```bash
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```
Expected: 1860/1860, boot golden matches. (Watch for gcc-only `-Wunused-variable` — the
LS-2 T3a class; fix-forward if it fires.)

- [ ] **Step 3: `make smoke-account` (MANDATORY — login/rent path touched)**

Run per the standing procedure (host run; afterwards restore the container ELF:
`docker compose run --rm --pull never rots bash -lc 'cp /rots/build/ageland /rots/bin/ageland'`
— the Mach-O-in-bin trap, hit twice before). Expected: the full 16-step flow passes. Note in
the PR: smoke-account guards against regression here; it cannot observe the m-14 change itself
(vnum 1152 resolves in shipped world data — the defect is latent, spec §2.1).

- [ ] **Step 4: i386 battery**

```bash
scripts/i386-battery.sh
```
Expected: 1860 total / 6 skips via ctest; monolithic reconciles per the standing method
(gtest-visible = 1860 − 11 CMake-ctest-only checks; 23 − 17 `ConvertEquivalence` skips leaves
the standing 6, +1 if the env-gated `LocationBenchmark` skip counts on this platform — record
what is measured); boot golden matches. Normalize any future-mtime stamps first
(`find src tools -newermt tomorrow -exec touch {} +` idiom) if sabotage probes left any.

- [ ] **Step 5: Fold the numbers into AGENTS.md + commit**

Append to the AGENTS.md LS-3b chain entry (after the PR #28 record), one sentence each:
the follow-up PR (branch, findings closed F-5/m-14, m-15 dispositioned), 1859 → **1860** (+1
`ResolvableBuggedRoomIsStillUsed`), `TOKEN_PATTERNS` ten → **eleven**, the registry + `--check`
assertion, and the measured i386/`rots64` numbers from Steps 2/4.

```bash
git add AGENTS.md
git commit -m "ls3b-minors: fold the follow-up's measured numbers into AGENTS.md"
```

- [ ] **Step 6: Push, open PR, request review**

```bash
git push -u origin fix/ls3b-review-minors
gh pr create --title "LS-3b deferred-MINORs follow-up: location-state registry (F-5) + bugged-arm clamp (m-14)" --body "<summary per repo PR conventions: changes, findings closed, validation steps incl. all Task 4 gate results, m-15 disposition line with the pointer at the M0/M1 harness + T5 baseline table>"
```
Then: adversarial implementation review per the standing practice; all six blocking CI jobs
green; merge is the owner's call.

---

## Self-review record

- Spec coverage: §1.1/1.5 → Task 2; §1.2 → Task 1; §1.3 → Task 2 (hermetic split honored:
  standing synthetic cases + gated one-time real-doc probe); §1.4 → no task (out of scope,
  correct); §2.1-2.5 → Task 3; §3 → Tasks 3 (per-change) + 4 (branch legs); §4 → task order
  arbitrary, plan runs 1→2→3 for gate-width convenience only; m-15 disposition → Task 4 Step 6.
- The one open verification point is flagged inline (Task 2 Step 6: whether `_run_gate` passes
  `--check` — the implementer must read it and pick the stated fallback if so).
- Type/name consistency: token name `ls_load_room_vnum_` (Tasks 1/2), registry marker/header
  strings (Task 2 Steps 1/2/4 identical), test names (Task 3 Steps 1-3 vs Step 4 expectations).
