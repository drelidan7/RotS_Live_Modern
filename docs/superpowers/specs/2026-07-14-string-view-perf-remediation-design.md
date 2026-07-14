# String-View Migration Performance Remediation — Design

**Date:** 2026-07-14
**Status:** Approved (all five sections user-approved in-session)
**Context:** The repository-string-view-first-pass branch review (multi-agent, high effort)
confirmed five performance regressions introduced by the migration: hot paths that were
zero-copy/zero-allocation on master gained per-call heap owners or redundant whole-input
scans. This spec covers their remediation, in severity order. The three correctness
defects from the same review were fixed separately (commit c8b367f); the sanitizer
presets now arm STL precondition checks (commit 93418ed).

## Cross-cutting decisions

- **Evidence bar (user-selected):** each fix that eliminates allocations carries an
  allocation-count regression test — a scoped `operator new`/`operator delete` counter
  (small test-only utility in `src/tests/`) wraps the fixed path and asserts an exact
  allocation count (zero in every asserted case; #7 is exempt below). Timing
  microbenchmarks are run once, manually,
  and quoted in commit messages; they are not committed as tests. Fixes whose win is
  scan-elimination rather than allocation-elimination (#7) rely on differential semantic
  tests instead, since the counter cannot observe scans.
- **First-null policy is preserved everywhere.** `rots::text::truncate_at_null` remains
  at every boundary; on a `std::string_view` it is a `substr` (one `memchr` scan, no
  copy). Only the *owning copies* staged behind it are removed.
- **Execution:** one fix per commit, in order #4 → #8. Per-fix gates: macos-arm64 build
  + ctest + boot golden, rots64 build + ctest + boot golden, macOS ASan preset for
  new/substantially rewritten test files. The qemu-i386 battery runs once at branch
  finalization per AGENTS.local.md.
- **TDD:** every fix starts from a failing (or newly-written-and-watched-red)
  allocation-count or differential test.

## Allocation-counter test utility

`src/tests/scoped_allocation_counter.h` (+ one `.cpp` for the replaced operators):
test-binary-global counting `operator new`/`operator delete` feeding a thread-local
tally, plus an RAII reader. Linked into `ageland_tests` only — GoogleTest remains
test-only tooling and nothing here touches the game binary. Usage:

```cpp
ScopedAllocationCounter counter;
convert_string(...);
EXPECT_EQ(counter.allocations(), 0u);
```

Sanitizer note: ASan replaces the allocator but still routes through the replaced
`operator new`, so counts stay stable under the sanitizer presets; if a platform proves
otherwise the assertion is gated to non-sanitizer builds for that case only, never
deleted.

## #4 — `act()`/`convert_string` per-recipient allocation (src/comm.cpp)

**Change:** replace the `std::string expanded_text` accumulator with a direct bounded
splice into `output_buffer`: a local `append(std::string_view)` lambda memcpys at a
running `write_index`, capped at `MAX_STRING_LENGTH - 1`. Plain-text runs between
`$`-tokens are located with `find('$')` and copied as whole segments (an improvement on
master's byte-at-a-time walk). The `$`-code switch, replacement sources (`PERS`,
`CC_USE`, pronoun tables), trailing `"\n\r"` + `CC_NORM`, and the ANSI-skipping
capitalization pass are unchanged — the capitalization pass runs over `output_buffer`
in place.

**Behavior contract:** output bytes identical for all inputs; oversized expansions
truncate at the same cap (truncation only ever drops trailing bytes, so cap-during-write
equals build-then-clip). The branch's bounds cap is retained — master's uncapped pointer
walk is not restored.

**Tests:** existing `act_format_tests` goldens pin bytes; new zero-allocation assertion
per `act()` recipient via `RoomPairContext`.

## #5 — `JsonReader`/`JsonReaderV2` whole-document copy (src/json_utils.{h,cpp})

**Change (user-selected: borrowed view + guard rail):** `m_input` becomes
`std::string_view` in both readers; constructor remains
`explicit JsonReader(std::string_view input) : m_input(rots::text::truncate_at_null(input))`
(scan kept, copy removed). Add `explicit JsonReader(std::string&&) = delete;` (and on
V2) so binding a reader to a `std::string` temporary is a compile error. The
constructor comment states the lifetime contract: the input buffer must outlive the
reader.

**Call-site audit:** every construction site (~15 across boards, mail, pkill, db,
character_json, objects_json, account_management_storage, and tests) is verified during
implementation; the dominant pattern (`JsonReader(json).parse_root_object(...)` over a
caller-owned lvalue) is lifetime-safe because the temporary reader dies at the end of
the full expression.

**Tests:** the owning-behavior pinning test
(`ReadersOwnShortLivedFirstNullTerminatedInput`) is replaced by (a)
`static_assert(!std::is_constructible_v<JsonReader, std::string&&>)` and (b) a
zero-allocation construction test over a multi-KB document. The `JsonPerf` V1-vs-V2
equality suite runs unchanged; the "untouched measurement baseline" comment is updated
since both readers change identically.

## #6 — `load_player_from_text` whole-file copy (src/db.cpp)

**Change:** drop `player_text_owner`; truncate the incoming view (substr) and walk it
with `const char* position` / existing `input_end`. The `const char*` cursor type makes
the compiler enforce the read-only walk.

**Hard gate before flipping:** audit every write through `position`/derived pointers.
The parser copies lines into its local `char line[100]` scratch, so a read-only walk is
expected; any in-place write found gets a local segment copy, never a document copy.

**Retained behavior divergence (deliberate):** empty (non-null) player text is rejected
with -1 (branch behavior), stricter than master's null-only guard — an empty player
file is corrupt data.

**Callers audited:** `load_player` (owns `pf`, RELEASEs after return) and
`account_management_internal.cpp` (owned buffer). Synchronous parse; no stored-reader
risk.

**Tests:** `db_save_roundtrip_tests` gains a zero-allocation case for a well-formed
minimal player text (error-logging branches are the only allocating paths and a clean
parse never hits them).

## #7 — `str_cmp`/`strn_cmp` double pre-scan (src/utility.cpp)

**Change, part (a):** view overloads drop both `truncate_at_null` pre-scans and fold
first-null semantics into the loop via a lazily materialized effective character —
out-of-range or embedded NUL reads as `'\0'` and terminates. Provably equivalent to
truncate-then-compare; single-pass, early-exit. `strn_cmp` folds its `count` bound into
the same shape.

**Change, part (b):** `str_cmp_nullable`/`strn_cmp_nullable` get a raw two-pointer
sentinel walk (zero scans of any kind), following the documented `isname_c_string`
precedent: nullable legacy callers provide null-terminated strings, and these wrappers
are what the player-table scan loops actually call.

**Tests:** existing bounded/embedded-NUL semantics tests, plus a differential table
asserting `str_cmp_nullable(a,b) == str_cmp(a,b)` (and the strn variants) across
ordering, prefix, embedded-NUL, and empty cases, mirroring the isname differential
pattern. No allocation assertion (these never allocated); one-off timing quoted in the
commit message.

## #8 — target lookups heap-allocate per probe (src/handler.cpp)

**Change:** new non-mutating helper next to `get_number`:

```cpp
struct NumberedName {
    int match_number;      // 0 = malformed prefix (legacy no-match), 1 = no prefix
    std::string_view name; // keyword after "N.", or the whole input
};
NumberedName parse_numbered_name(std::string_view input);
```

Semantics transliterated from `get_number`: no dot → `{1, input}`; `"2.sword"` →
`{2, "sword"}`; non-digit or empty prefix → `{0, …}`; first dot wins
(`"2.3.sword"` → `{2, "3.sword"}`). Conversion via `std::from_chars`; failure → 0
(documented tightening of legacy `atoi` overflow UB).

`get_char`, `get_obj_in_list_vis`, and every other `mutable_name` site found in the
implementation sweep drop their copies and match with the view `isname` (now exactly
equivalent to the legacy matcher and differentially tested). Null candidate namelists
are skipped explicitly. `get_number` itself stays for its remaining C-string callers.

**Noted trade-off:** the view `isname` pays one `strlen` per candidate namelist that
the nullable sentinel walk didn't; namelists are short and the matcher walks them
anyway, while a per-probe copy/allocation disappears. If the one-off timing pass shows
it mattering, the fallback is a `(string_view, const char*)` sentinel-walk overload —
not built speculatively.

**Tests:** differential unit table `parse_numbered_name` vs mutating `get_number`
(including `".sword"`, `"x.sword"`, `"2.3.sword"`, `""`); zero-allocation assertion for
`get_obj_in_list_vis` with a >SSO numbered keyword; existing targeting tests pin
results.

## Backlog (explicitly out of scope for this wave)

- **`std::format_to_n` composition sweep (user-requested):** audit hot-path text
  *composition* sites that pay a `std::format` return-string allocation or go through
  `sprintf` (prompt builder, per-hit combat log lines), and convert them to
  `std::format_to_n` into stack buffers. Separate wave; none of the five findings above
  is a composition site.
- Review findings #9 (`sprinttype` strcpy on view data) and #10 (writers'
  `truncate_at_null` on file contents) are latent-hazard cleanups, tracked separately.

## Success criteria

1. Allocation-count tests assert zero allocations on all four allocation paths
   (#4, #5, #6, #8); differential tables pin #7 and #8 semantics.
2. All existing suites green on macos-arm64, macos-arm64-asan (hardened), rots64,
   linux-x64-sanitize (hardened); boot goldens byte-identical on both platforms.
3. Characterization goldens unchanged (no intentional behavior drift in this wave).
4. One commit per fix, timing numbers quoted per commit message.
