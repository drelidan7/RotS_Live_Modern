# String-View Performance Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the five hot-path performance regressions the string-view migration introduced (spec: `docs/superpowers/specs/2026-07-14-string-view-perf-remediation-design.md`), each guarded by an allocation-count or differential regression test.

**Architecture:** One shared test-only allocation counter, then five independent fixes restoring master's zero-copy shapes while keeping the migration's bounds safety and first-null policy. One commit per task.

**Tech Stack:** C++20, GoogleTest (test-only), CMake presets + legacy Makefiles, `rots::text::truncate_at_null` boundary policy.

## Global Constraints

- All builds are `-Wall -Wextra -Werror` (GNU) / `/W4 /WX` (MSVC): a warning is a failure.
- `rots::text::truncate_at_null` stays at every textual boundary; only owning *copies* are removed, never the normalization.
- GoogleTest and everything under `src/tests/` is test-only; nothing in this plan may link into the `ageland` game binary.
- Class-scoped variables (C++ data members) require a `//` comment describing role and use (user convention; applies to new structs and members, production and tests).
- **Formatter-hook hazard:** the machine's PostToolUse hook reformats any `.cpp`/`.h` edited via the Edit/Write tools using the repo-root `.clang-format` (LLVM-ish), which conflicts with the committed WebKit-ish style and churns whole files. Apply production-source edits via `python3` exact-string-replacement scripts run through Bash (read file, assert `count(old) == 1`, replace, write). **`src/handler.cpp` has mixed CRLF/LF line endings — edit it with `read_bytes`/`write_bytes` only.** New test files created via Write are fine if immediately committed before any Edit touches them; prefer python-script edits for existing files.
- Per-task verification gates (from AGENTS.local.md): native macOS `cmake --build --preset macos-arm64 && ctest --preset macos-arm64`; rots64 container build + ctest; boot goldens on both (`scripts/boot-golden.sh --native build/macos-arm64/ageland verify` and `--service rots64 verify`); `macos-arm64-asan` ctest whenever the task adds or substantially rewrites a test file (every task here). The qemu-i386 battery is finalization-only — do NOT run per task.
- Expected test counts: 1221 tests, 0 failures, on macos-arm64, macos-arm64-asan, rots64 linux-x64, and (if run) linux-x64-sanitize.
- Commit messages: imperative subject ≤72 chars, ending with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_0175SSLovwVtVAknFkBPYFps`
- Timing evidence: run the fixed path once before/after under a crude timer if convenient and quote numbers in the commit message; do NOT commit timing tests.

---

### Task 1: Scoped allocation counter test utility

**Files:**
- Create: `src/tests/scoped_allocation_counter.h`
- Create: `src/tests/scoped_allocation_counter.cpp`
- Create: `src/tests/scoped_allocation_counter_tests.cpp`
- Modify: `src/CMakeLists.txt` (ROTS_TEST_SOURCES list, ~line 217)
- Modify: `src/tests/Makefile` (SRCS list, line 286)

**Interfaces:**
- Produces: `rots_test::ScopedAllocationCounter` with `std::size_t allocations() const` — snapshot-at-construction counter of global `operator new` calls on this thread. Later tasks (2, 3, 4, 6) include `"scoped_allocation_counter.h"` (path from `src/tests/`) and assert `counter.allocations() == 0u`.

- [ ] **Step 1: Confirm no existing global `operator new` replacement (ODR safety)**

Run: `grep -rn "operator new" src/ --include='*.cpp' --include='*.h'`
Expected: no global replacements (verified during planning; re-check in case the branch moved).

- [ ] **Step 2: Write the failing self-test**

Create `src/tests/scoped_allocation_counter_tests.cpp`:

```cpp
#include "scoped_allocation_counter.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

TEST(ScopedAllocationCounter, CountsHeapAllocationsSinceConstruction)
{
    rots_test::ScopedAllocationCounter counter;
    EXPECT_EQ(counter.allocations(), 0u);

    // A 64-byte string defeats SSO on every supported standard library.
    const std::string forced_allocation(64, 'x');
    EXPECT_GE(counter.allocations(), 1u);
}

TEST(ScopedAllocationCounter, SeparateCountersSnapshotIndependently)
{
    rots_test::ScopedAllocationCounter outer;
    const auto first_heap_object = std::make_unique<int>(7);
    rots_test::ScopedAllocationCounter inner;
    EXPECT_EQ(inner.allocations(), 0u);
    EXPECT_GE(outer.allocations(), 1u);
    EXPECT_EQ(*first_heap_object, 7);
}
```

- [ ] **Step 3: Run to verify it fails (missing header)**

Run: `cd src && cmake --build --preset macos-arm64 -j4 --target ageland_tests`
Expected: FAIL — `scoped_allocation_counter.h` not found (after adding the file to ROTS_TEST_SOURCES in this step: add `tests/scoped_allocation_counter.cpp` and `tests/scoped_allocation_counter_tests.cpp` alphabetically to the list; also append `scoped_allocation_counter.cpp scoped_allocation_counter_tests.cpp` to `SRCS` in `src/tests/Makefile:286`).

- [ ] **Step 4: Write the utility**

Create `src/tests/scoped_allocation_counter.h`:

```cpp
#pragma once

#include <cstddef>

namespace rots_test {

// Reads the thread-local tally maintained by the replaced global allocation
// functions in scoped_allocation_counter.cpp (test binary only).
std::size_t current_allocation_count();

// Snapshots the allocation tally at construction so allocations() reports how
// many operator-new calls this thread has made since the counter was created.
class ScopedAllocationCounter {
public:
    ScopedAllocationCounter()
        : m_start(current_allocation_count())
    {
    }

    std::size_t allocations() const { return current_allocation_count() - m_start; }

private:
    // Tally captured at construction; baseline subtracted by allocations().
    std::size_t m_start;
};

} // namespace rots_test
```

Create `src/tests/scoped_allocation_counter.cpp`:

```cpp
// Test-binary-global replacement of the allocation functions, feeding a
// thread-local tally that ScopedAllocationCounter snapshots. Linked into
// ageland_tests only -- never into the game binary. Aligned-allocation
// overloads are deliberately not replaced: no asserted path allocates
// over-aligned storage, and the platform defaults remain correct.

#include "scoped_allocation_counter.h"

#include <cstdlib>
#include <new>

namespace {

// Number of operator-new calls made on this thread since process start.
thread_local std::size_t allocation_count = 0;

void* counted_allocate(std::size_t size) noexcept
{
    ++allocation_count;
    return std::malloc(size != 0 ? size : 1);
}

} // namespace

namespace rots_test {

std::size_t current_allocation_count() { return allocation_count; }

} // namespace rots_test

void* operator new(std::size_t size)
{
    if (void* pointer = counted_allocate(size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    if (void* pointer = counted_allocate(size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted_allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted_allocate(size); }

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
```

- [ ] **Step 5: Build and run the self-test**

Run: `cd src && cmake --build --preset macos-arm64 -j4 --target ageland_tests && ../build/macos-arm64/ageland_tests --gtest_filter='ScopedAllocationCounter.*'`
Expected: 2 tests PASS.

- [ ] **Step 6: Full-suite sanity (the replaced operators now serve every test)**

Run: `ctest --preset macos-arm64` — expected 1221/1221.
Run the macos-arm64-asan build + ctest (new test file ⇒ sanitizer gate). If the two self-tests prove flaky under ASan's allocator, gate ONLY those assertions with `#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)` and note it in the commit; do not delete them.

- [ ] **Step 7: rots64 gate + boot goldens (both platforms), then commit**

```bash
git add src/tests/scoped_allocation_counter.h src/tests/scoped_allocation_counter.cpp src/tests/scoped_allocation_counter_tests.cpp src/CMakeLists.txt src/tests/Makefile
git commit -m "test: add scoped allocation counter utility"
```

---

### Task 2: Fix #4 — `convert_string` bounded splice, zero allocations

**Files:**
- Modify: `src/comm.cpp` (convert_string, ~lines 2205–2390)
- Test: `src/tests/act_format_tests.cpp`

**Interfaces:**
- Consumes: `rots_test::ScopedAllocationCounter` (Task 1).
- Produces: no interface change — `convert_string`/`act()` signatures and output bytes unchanged.

- [ ] **Step 1: Write the failing allocation test**

Append to `src/tests/act_format_tests.cpp` (uses the existing `RoomPairContext` fixture; include `"scoped_allocation_counter.h"` with the file's other test includes):

```cpp
TEST(GameplaySpeechText, ActExpansionPerformsNoHeapAllocations)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("speaker");
    context.victim.player.name = const_cast<char*>("listener");

    rots_test::ScopedAllocationCounter counter;
    act("$n nods at $N.", FALSE, &context.actor, nullptr, &context.victim, TO_VICT, FALSE);
    EXPECT_EQ(counter.allocations(), 0u);

    EXPECT_EQ(std::string(context.victim_descriptor.output), "Speaker nods at listener.\n\r");
}
```

- [ ] **Step 2: Run to verify it fails for the right reason**

Run: `../build/macos-arm64/ageland_tests --gtest_filter='GameplaySpeechText.ActExpansionPerformsNoHeapAllocations'`
Expected: FAIL — allocations ≥ 1 (the `expanded_text` reserve/growth). The output-bytes expectation must PASS; if it doesn't, fix the expected string to the actual bytes before proceeding (behavior is pinned as-is, never adjusted).

- [ ] **Step 3: Rewrite convert_string as a bounded splice (python-script edit)**

The `$`-code switch (from `switch (token) {` through its closing brace, currently producing `replacement`) moves **verbatim** — do not retype it. Replace the surrounding mechanism so the function reads:

```cpp
void convert_string(std::string_view format_text, int, struct char_data* ch,
    struct obj_data* obj, void* vict_obj, struct char_data* to, char* output_buffer)
{
    bool clobbered_color = false;
    const char* used_color = nullptr;
    std::size_t write_index = 0;

    // Bounded splice into the caller's MAX_STRING_LENGTH buffer: truncation
    // during the write is byte-identical to the old build-then-clip because
    // overflow only ever drops trailing bytes.
    const auto append = [&](std::string_view text) {
        const std::size_t space = static_cast<std::size_t>(MAX_STRING_LENGTH - 1) - write_index;
        const std::size_t copy_size = std::min(text.size(), space);
        std::memcpy(output_buffer + write_index, text.data(), copy_size);
        write_index += copy_size;
    };

    for (std::size_t format_index = 0; format_index < format_text.size(); ++format_index) {
        const std::size_t dollar_position = format_text.find('$', format_index);
        if (dollar_position != format_index) {
            // Copy the whole plain-text run up to the next token (or the end) at once.
            append(format_text.substr(format_index,
                (dollar_position == std::string_view::npos ? format_text.size() : dollar_position)
                    - format_index));
            if (dollar_position == std::string_view::npos) {
                break;
            }
            format_index = dollar_position;
        }
        if (format_index + 1 >= format_text.size()) {
            append(format_text.substr(format_index, 1));
            break;
        }
        const char token = format_text[++format_index];
        const char* replacement = nullptr;
        /* ===== existing switch (token) { ... } body moves here verbatim ===== */
        if (replacement != nullptr) {
            append(replacement);
        }
        if (clobbered_color && used_color != nullptr) {
            append(used_color);
            clobbered_color = false;
        }
    }

    append("\n\r");
    if (used_color) {
        append(CC_NORM(to));
    }
    output_buffer[write_index] = '\0';

    /* Find the first character in the string, ignoring ANSI colors */
    std::size_t visible_index = 0;
    while (visible_index < write_index && output_buffer[visible_index] == '\x1B') {
        const char* color_end
            = static_cast<const char*>(std::memchr(output_buffer + visible_index, 'm', write_index - visible_index));
        if (color_end == nullptr) {
            break;
        }
        visible_index = static_cast<std::size_t>(color_end - output_buffer) + 1;
    }
    if (visible_index < write_index && isalpha(static_cast<unsigned char>(output_buffer[visible_index]))) {
        output_buffer[visible_index]
            = static_cast<char>(toupper(static_cast<unsigned char>(output_buffer[visible_index])));
    }
}
```

Notes for the implementer:
- Inside the moved switch, the `$C` color arm indexes `format_text[++format_index]` — keep its existing incomplete-code guard exactly as-is.
- `CC_NORM(to)` — check its return type at `src/color.h`; if it returns `std::string_view` the `append` call is direct; if `const char*`, `append` still takes it via implicit view conversion.
- The old lines that built `expanded_text`, the final `std::min`/`memcpy` block, and the string-based capitalization pass are all deleted.
- Delete the now-unused `#include` only if `<string>` has no other user in comm.cpp (grep first).

- [ ] **Step 4: Build, run the new test and the act/speech suites**

Run: `cd src && cmake --build --preset macos-arm64 -j4 --target ageland_tests && ../build/macos-arm64/ageland_tests --gtest_filter='GameplaySpeechText.*:ActFormat*:CommAct*'`
Expected: all PASS, including zero allocations.

- [ ] **Step 5: Full gates**

macos-arm64 ctest (1221/1221) + boot golden; rots64 build + ctest + boot golden; macos-arm64-asan ctest (test file modified). Characterization goldens must be untouched.

- [ ] **Step 6: Commit**

```bash
git add src/comm.cpp src/tests/act_format_tests.cpp
git commit -m "perf: splice act() expansions without heap allocation"
```

---

### Task 3: Fix #5 — JsonReader/JsonReaderV2 borrow their input

**Files:**
- Modify: `src/json_utils.h` (JsonReader ~lines 23–84, JsonReaderV2 ~lines 90–160)
- Modify: `src/json_utils.cpp` (both constructors, ~line 152 and the V2 ctor)
- Test: `src/tests/json_utils_tests.cpp`

**Interfaces:**
- Consumes: `rots_test::ScopedAllocationCounter` (Task 1).
- Produces: `JsonReader(std::string_view)` now borrows; `JsonReader(std::string&&) = delete;` (same for V2). Lifetime contract: input buffer must outlive the reader.

- [ ] **Step 1: Write the failing allocation test**

Append to `src/tests/json_utils_tests.cpp` (include `"scoped_allocation_counter.h"`):

```cpp
TEST(JsonUtils, ReadersBorrowInputWithoutAllocating)
{
    std::string document = "{\"value\":42}";
    document.append(4096, ' ');

    rots_test::ScopedAllocationCounter counter;
    json_utils::JsonReader reader(document);
    json_utils::JsonReaderV2 reader_v2(document);
    EXPECT_EQ(counter.allocations(), 0u);
}
```

- [ ] **Step 2: Run to verify it fails (owning copy allocates)**

Run: `../build/macos-arm64/ageland_tests --gtest_filter='JsonUtils.ReadersBorrowInputWithoutAllocating'`
Expected: FAIL with allocations ≥ 2 (one owned copy per reader).

- [ ] **Step 3: Flip both readers to borrowed views (python-script edits)**

In `src/json_utils.h`, for **both** classes:
- Member: replace
  `// Owns the bounded JSON prefix being parsed; never includes bytes after the first null.`
  `std::string m_input;`
  with
  `// Borrows the caller's bounded JSON prefix (never past the first null); the input buffer must outlive the reader.`
  `std::string_view m_input;`
- Constructor docs + guard rail: replace the ctor declaration block with

```cpp
    /// Borrows a bounded JSON document and parses only its prefix before the first
    /// null byte. The viewed buffer must outlive the reader.
    explicit JsonReader(std::string_view input);
    /// Binding a reader to a std::string temporary would dangle immediately.
    explicit JsonReader(std::string&&) = delete;
```

(identically for `JsonReaderV2`, adjusting the class name). Update the V2 class-comment sentence about "lower-allocation internals" to note both readers now borrow.

In `src/json_utils.cpp`, both constructors keep their body shape:
`: m_input(rots::text::truncate_at_null(input)) {}` — now initializing a view (substr, no copy). Update the JsonReader ctor's comment (currently "Readers retain their input... one construction-time copy is preferable...") to:
`// Borrows the caller's buffer (truncate_at_null on a view is a substr): the lifetime contract is enforced socially by the deleted std::string&& overload and this comment.`

- [ ] **Step 4: Build — the compiler runs the call-site audit**

Run: `cd src && cmake --build --preset macos-arm64 -j4 --target ageland_tests 2>&1 | grep -E "error" | head -30`
Every error is either (a) a construction from a `std::string` rvalue — give the temporary a named local that outlives the reader — or (b) the removed owning test helper (next step). Expect the helper `expect_reader_owns_short_lived_input` and test `ReadersOwnShortLivedFirstNullTerminatedInput` in `json_utils_tests.cpp` to fail: **delete both** (the lambda returns a reader over a dead local — exactly what the new contract forbids) and replace with:

```cpp
// The readers borrow; constructing one from a std::string temporary must not compile.
static_assert(!std::is_constructible_v<json_utils::JsonReader, std::string&&>);
static_assert(!std::is_constructible_v<json_utils::JsonReaderV2, std::string&&>);
static_assert(std::is_constructible_v<json_utils::JsonReader, const std::string&>);
static_assert(std::is_constructible_v<json_utils::JsonReaderV2, const std::string&>);
```

(add `#include <type_traits>` if missing). Also run a manual sweep for view-of-temporary construction the deleted overload cannot catch:
`grep -rn "JsonReader(\|JsonReaderV2(" src --include='*.cpp' --include='*.h' | grep -v "json_utils\|_tests"` — verify each input is a caller-owned lvalue.

- [ ] **Step 5: Run the JSON suites + allocation test**

Run: `../build/macos-arm64/ageland_tests --gtest_filter='JsonUtils.*:JsonPerf.*:CharacterizationJson*'`
Expected: all PASS; `ReadersBorrowInputWithoutAllocating` now green.

- [ ] **Step 6: Full gates** (as Task 2 Step 5 — all four suites + both boot goldens; JSON goldens byte-identical).

- [ ] **Step 7: Commit**

```bash
git add src/json_utils.h src/json_utils.cpp src/tests/json_utils_tests.cpp
git commit -m "perf: borrow JSON reader input instead of copying"
```

---

### Task 4: Fix #6 — `load_player_from_text` parses the caller's buffer

**Files:**
- Modify: `src/db.cpp` (load_player_from_text, ~lines 2168–2470; LOAD-macros region ~line 2089)
- Test: `src/tests/db_loader_tests.cpp`

**Interfaces:**
- Consumes: `rots_test::ScopedAllocationCounter` (Task 1).
- Produces: no signature change; `load_player_from_text(char*, std::string_view, char_file_u*)` now borrows.

- [ ] **Step 1: Write the failing allocation test**

Append to `src/tests/db_loader_tests.cpp`, mirroring `LegacyPlayerTextRoundTripPreservesCombatState`'s fixture exactly (include `"scoped_allocation_counter.h"`):

```cpp
TEST(DbLoader, LegacyPlayerTextLoadPerformsNoHeapAllocations)
{
    TemporaryDirectory temp_directory;
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/players").c_str()));
    ASSERT_TRUE(
        std::filesystem::create_directory((temp_directory.path() + "/players/A-E").c_str()));

    char_file_u original = make_stored_character("aragorn");
    const std::string player_text = write_valid_legacy_player_file(temp_directory.path(), original);

    char player_name[] = "aragorn";
    char_file_u loaded {};
    ScopedPlayerTableEntry player_table_entry("aragorn");

    rots_test::ScopedAllocationCounter counter;
    ASSERT_EQ(load_player_from_text(player_name, player_text, &loaded), 1);
    EXPECT_EQ(counter.allocations(), 0u);
}
```

- [ ] **Step 2: Run to verify it fails** — expected: allocations ≥ 1 (the `player_text_owner` copy).

- [ ] **Step 3: Borrow the buffer (python-script edit + compiler-driven const propagation)**

In `load_player_from_text` (`src/db.cpp:2170` region):
- Replace `std::string player_text_owner(rots::text::truncate_at_null(player_text));` with `player_text = rots::text::truncate_at_null(player_text);`
- Replace `if (player_text_owner.empty())` with `if (player_text.empty())` (the stricter empty-text rejection is deliberate and stays — spec).
- Replace `input_end = player_text_owner.data() + player_text_owner.size();` with `input_end = player_text.data() + player_text.size();`
- Replace `position = player_text_owner.data();` with `position = player_text.data();`
- Change the cursor declarations `char *tmpchar, *value, *ctmp, *position;` → keep `tmpchar` as `char*` (it lowercases `name`, which is genuinely mutable), and split out `const char* value; const char* ctmp; const char* position;`.
- Build. **Hard gate:** every compile error must be resolvable by adding `const` (pointers into the input) — if any error demands a *write* through an input pointer, STOP, record the site, and copy only that segment locally per the spec. Do not const_cast. The `LOAD_STRING`-style macros near `src/db.cpp:2089` may need their local pointer types consted too.

- [ ] **Step 4: Run the loader suites**

Run: `../build/macos-arm64/ageland_tests --gtest_filter='DbLoader*:DbSaveRoundtrip*'`
Expected: all PASS including the new zero-allocation case.

- [ ] **Step 5: Full gates** (all four suites + both boot goldens — boot exercises `build_player_index`, the real beneficiary).

- [ ] **Step 6: Commit**

```bash
git add src/db.cpp src/tests/db_loader_tests.cpp
git commit -m "perf: parse legacy player text without copying"
```

---

### Task 5: Fix #7 — single-pass `str_cmp`/`strn_cmp`, sentinel-walk nullable wrappers

**Files:**
- Modify: `src/utility.cpp:1176-1250` (all four comparators)
- Test: `src/tests/string_view_utility_tests.cpp`

**Interfaces:**
- Produces: no signature change; semantics pinned identical by the differential table.

- [ ] **Step 1: Add the differential pinning tests (characterization — expect PASS immediately)**

Append to `src/tests/string_view_utility_tests.cpp`:

```cpp
TEST(StringViewUtility, NullableComparisonsMatchViewComparisons)
{
    constexpr std::array<std::pair<const char*, const char*>, 12> comparison_cases { {
        { "alpha", "alpha" },
        { "alpha", "ALPHA" },
        { "alpha", "alphabet" },
        { "alphabet", "alpha" },
        { "", "" },
        { "", "x" },
        { "x", "" },
        { "abc", "abd" },
        { "abd", "abc" },
        { "Zeta", "alpha" },
        { "alpha", "Zeta" },
        { "same", "sameness" },
    } };

    for (const auto& [first, second] : comparison_cases) {
        EXPECT_EQ(str_cmp_nullable(first, second), str_cmp(first, second))
            << first << " vs " << second;
        for (const int count : { 0, 1, 3, 100 }) {
            EXPECT_EQ(strn_cmp_nullable(first, second, count), strn_cmp(first, second, count))
                << first << " vs " << second << " n=" << count;
        }
    }
}
```

Run the suite; expected: PASS (both implementations agree today). This is the guard that keeps them agreeing after the rewrite. The existing `ComparisonStopsAtFirstEmbeddedNull` / bounded-view tests stay untouched and keep pinning view semantics.

- [ ] **Step 2: Rewrite the four comparators (python-script edit)**

Replace `str_cmp` (src/utility.cpp:1176) body:

```cpp
/* returns: 0 if equal, 1 if arg1 > arg2, -1 if arg1 < arg2  */
/* scan 'till found different or end of both                 */
int str_cmp(std::string_view first, std::string_view second)
{
    // First-null semantics are folded into the loop: past-the-end or an
    // embedded null both read as '\0', exactly where truncate_at_null would
    // have cut -- without pre-scanning either argument.
    for (std::size_t index = 0;; ++index) {
        const char first_char = (index < first.size()) ? first[index] : '\0';
        const char second_char = (index < second.size()) ? second[index] : '\0';
        if (first_char == '\0' || second_char == '\0') {
            if (first_char == second_char) {
                return 0;
            }
            return (first_char == '\0') ? -1 : 1;
        }
        const int difference = LOWER(first_char) - LOWER(second_char);
        if (difference < 0) {
            return -1;
        }
        if (difference > 0) {
            return 1;
        }
    }
}
```

Replace `strn_cmp` (same file, ~line 1212) body:

```cpp
/* returns: 0 if equal, 1 if arg1 > arg2, -1 if arg1 < arg2  */
/* scan 'till found different, end of both, or n reached     */
int strn_cmp(std::string_view first, std::string_view second, int count)
{
    if (count <= 0) {
        return 0;
    }
    const std::size_t comparison_limit = static_cast<std::size_t>(count);
    for (std::size_t index = 0; index < comparison_limit; ++index) {
        const char first_char = (index < first.size()) ? first[index] : '\0';
        const char second_char = (index < second.size()) ? second[index] : '\0';
        if (first_char == '\0' || second_char == '\0') {
            if (first_char == second_char) {
                return 0;
            }
            return (first_char == '\0') ? -1 : 1;
        }
        const int difference = LOWER(first_char) - LOWER(second_char);
        if (difference < 0) {
            return -1;
        }
        if (difference > 0) {
            return 1;
        }
    }
    return 0;
}
```

Replace `str_cmp_nullable` body (keep its existing null-handling prologue verbatim, then):

```cpp
    // Nullable legacy callers provide null-terminated strings (see the
    // isname_c_string precedent): a raw sentinel walk avoids constructing and
    // scanning two views on the player-table lookup paths.
    for (;; ++first, ++second) {
        const int difference = LOWER(*first) - LOWER(*second);
        if (difference != 0) {
            return (difference < 0) ? -1 : 1;
        }
        if (*first == '\0') {
            return 0;
        }
    }
```

Replace `strn_cmp_nullable` body (keep null prologue, then):

```cpp
    if (count <= 0) {
        return 0;
    }
    for (int index = 0; index < count; ++first, ++second, ++index) {
        const int difference = LOWER(*first) - LOWER(*second);
        if (difference != 0) {
            return (difference < 0) ? -1 : 1;
        }
        if (*first == '\0') {
            return 0;
        }
    }
    return 0;
```

- [ ] **Step 3: Run comparison + differential suites**

Run: `../build/macos-arm64/ageland_tests --gtest_filter='StringViewUtility.*'`
Expected: all PASS (including the pre-existing embedded-null/bounded tests and the new differential table).

- [ ] **Step 4: One-off timing note** — time 10M `str_cmp_nullable("gandalf", "gandalf-the-white")` iterations before/after (scratch program or crude loop in a disposable test run); quote in the commit message. Do not commit the timing code.

- [ ] **Step 5: Full gates** (all four suites + both boot goldens).

- [ ] **Step 6: Commit**

```bash
git add src/utility.cpp src/tests/string_view_utility_tests.cpp
git commit -m "perf: make string comparisons single-pass early-exit"
```

---

### Task 6: Fix #8 — non-mutating `parse_numbered_name`, zero-copy target lookups

**Files:**
- Modify: `src/handler.h` (declarations near `int isname(...)`, line ~55)
- Modify: `src/handler.cpp` (new helper near `get_number` ~line 1541; `get_char` ~line 1700; `get_obj_in_list_vis` ~line 2240; every other `mutable_name` site found by the sweep) — **byte-level edits only (mixed CRLF/LF)**
- Test: `src/tests/string_view_utility_tests.cpp`

**Interfaces:**
- Consumes: `rots_test::ScopedAllocationCounter` (Task 1); the view `isname` (exact legacy equivalence guaranteed by the c8b367f fix and its differential tests).
- Produces: `struct NumberedName { int match_number; std::string_view name; };` and `NumberedName parse_numbered_name(std::string_view input);` declared in `src/handler.h`.

- [ ] **Step 1: Write the failing tests (differential + zero-allocation)**

Append to `src/tests/string_view_utility_tests.cpp`. First check `grep -n "get_number" src/handler.h src/utils.h` — if `int get_number(char** name);` is not declared in a header, declare it locally in the test file with a comment. Add `#include "../db.h"` if `character_list` is not already visible (verify with `grep -rn "extern.*character_list" src/*.h`).

```cpp
TEST(StringViewUtility, ParseNumberedNameMatchesLegacyGetNumber)
{
    constexpr std::array<const char*, 9> numbered_cases {
        "sword", "2.sword", ".sword", "x.sword", "2.3.sword",
        "0.sword", "12.long name", "7.", ""
    };

    for (const char* text : numbered_cases) {
        char legacy_buffer[MAX_INPUT_LENGTH];
        std::strcpy(legacy_buffer, text);
        char* legacy_cursor = legacy_buffer;
        const int legacy_number = get_number(&legacy_cursor);

        const auto parsed = parse_numbered_name(text);
        EXPECT_EQ(parsed.match_number, legacy_number) << text;
        if (legacy_number != 0) {
            EXPECT_EQ(parsed.name, std::string_view(legacy_cursor)) << text;
        }
    }
}

TEST(StringViewUtility, GetCharLookupPerformsNoHeapAllocations)
{
    char_data lookup_target {};
    lookup_target.player.name = const_cast<char*>("silverbeard-the-elder-of-erebor");
    char_data* const saved_character_list = character_list;
    character_list = &lookup_target;

    rots_test::ScopedAllocationCounter counter;
    char_data* const found = get_char("1.silverbeard-the-elder-of-erebor");
    EXPECT_EQ(counter.allocations(), 0u);
    EXPECT_EQ(found, &lookup_target);

    character_list = saved_character_list;
}
```

- [ ] **Step 2: Run to verify both fail correctly**

Expected: `ParseNumberedNameMatchesLegacyGetNumber` fails to link (`parse_numbered_name` undefined); comment it out momentarily to confirm `GetCharLookupPerformsNoHeapAllocations` fails with allocations ≥ 1 (the 33-char `mutable_name` defeats SSO on both standard libraries), then restore it.

- [ ] **Step 3: Add the helper (byte-level python edit)**

In `src/handler.h`, after the `isname`/`isname_nullable` declarations:

```cpp
// Non-mutating replacement for get_number's "N.keyword" prefix parse.
struct NumberedName {
    // Requested match ordinal from the "N." prefix: 0 = malformed prefix
    // (legacy no-match), 1 = no prefix present.
    int match_number;
    // Keyword after the prefix (whole input when no prefix); borrows the
    // caller's storage, bounded and first-null-normalized.
    std::string_view name;
};
NumberedName parse_numbered_name(std::string_view input);
```

In `src/handler.cpp`, directly below `get_number`:

```cpp
NumberedName parse_numbered_name(std::string_view input)
{
    input = rots::text::truncate_at_null(input);
    const std::size_t dot_position = input.find('.');
    if (dot_position == std::string_view::npos) {
        return { 1, input };
    }

    const std::string_view digits = input.substr(0, dot_position);
    const std::string_view remainder = input.substr(dot_position + 1);
    int parsed_number = 0;
    const auto [parse_end, parse_error]
        = std::from_chars(digits.data(), digits.data() + digits.size(), parsed_number);
    if (parse_error != std::errc() || parse_end != digits.data() + digits.size()) {
        // Non-digit, empty ("."), or overflowing prefix: legacy atoi produced 0
        // (no match) for the first two; overflow is tightened to the same result.
        return { 0, remainder };
    }
    return { parsed_number, remainder };
}
```

(add `#include <charconv>` to handler.cpp's includes if absent). Note `"0.sword"` parses to 0 — identical to legacy (`atoi("0")`), meaning no-match.

- [ ] **Step 4: Rewrite the lookup sites (byte-level python edit)**

Sweep: `grep -n "mutable_name" src/handler.cpp` — every hit gets the same transformation. For `get_char` (~line 1700):

```cpp
/* search all over the world for a char, and return a pointer if found */
struct char_data* get_char(std::string_view name)
{
    const auto [requested_match_number, query] = parse_numbered_name(name);
    if (requested_match_number == 0) {
        return (0);
    }

    int match_index = 1;
    for (char_data* candidate = character_list;
         candidate != nullptr && match_index <= requested_match_number;
         candidate = candidate->next) {
        if (candidate->player.name != nullptr && isname(query, candidate->player.name)) {
            if (match_index == requested_match_number) {
                return candidate;
            }
            ++match_index;
        }
    }
    return (0);
}
```

For `get_obj_in_list_vis` (~line 2240), same head, and the match line becomes:

```cpp
        if (candidate_object->name != nullptr && isname(query, candidate_object->name, 0)
            && CAN_SEE_OBJ(ch, candidate_object)) {
```

Every other `mutable_name` hit follows the identical pattern: destructure `parse_numbered_name`, early-return on 0, replace `isname_nullable(mutable_name_cursor, X, ...)` with a null-guard plus `isname(query, X, ...)` preserving each site's `full` argument. `get_number` itself and its remaining stack-buffer C-string callers stay untouched.

- [ ] **Step 5: Build and run the new tests + targeting suites**

Run: `../build/macos-arm64/ageland_tests --gtest_filter='StringViewUtility.*:CharUtils*:ObjectUtils*'`
Expected: all PASS, both new tests green.

- [ ] **Step 6: Full gates** (all four suites + both boot goldens).

- [ ] **Step 7: Commit**

```bash
git add src/handler.h src/handler.cpp src/tests/string_view_utility_tests.cpp
git commit -m "perf: parse numbered targets without copying the keyword"
```

---

## Completion checklist (after Task 6)

- [ ] All five spec fixes landed, one commit each, allocation/differential guards in place.
- [ ] `python3 tools/string_view_census.py --check` still exits 0 (textual interfaces changed in Tasks 3–6).
- [ ] Full CI matrix (push) green at the branch's next finalization point; qemu-i386 battery remains a finalization-only gate per AGENTS.local.md.
- [ ] Backlog items recorded in the spec (format_to_n composition sweep; review findings #9/#10) — not part of this plan.
