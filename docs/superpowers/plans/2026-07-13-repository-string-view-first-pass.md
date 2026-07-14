# Repository-Wide `std::string_view` First-Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert every eligible borrowed text parameter, named text constant, and lookup table in the repository to `std::string_view` while preserving first-null termination, ownership, persistence, protocol, and output behavior.

**Architecture:** Introduce one shared first-null normalization helper, then migrate APIs in dependency order so each layer compiles, passes focused characterization tests, and commits independently. Views remain borrowed inside C++ call chains; mutable, nullable-state, owning, binary, varargs, ABI, and genuine C boundaries remain explicit documented exceptions.

**Tech Stack:** C++20, `std::string_view`, `std::array`, GoogleTest/CTest, CMake presets, Python 3 census tooling, AppleClang/libc++, GCC/libstdc++, MSVC.

## Global Constraints

- Preserve textual C-string behavior: every semantic consumer stops at the first embedded `\0`.
- Explicit binary and length-delimited APIs retain all bytes.
- Change eligible signatures in place; do not retain compatibility overloads merely for source compatibility.
- Never construct `std::string_view` from a potentially null pointer.
- Never retain a view or its `data()` pointer beyond the call; copy into owning storage when retention is required.
- Never treat `std::string_view::data()` as null-terminated.
- Keep mutable `char*`/`char**`, ownership-transfer, nullable-state, printf-varargs, ABI-layout, and external C contracts until their semantics are separately redesigned.
- Preserve persistence bytes, protocol bytes, characterization goldens, and gameplay behavior.
- Do not regenerate `legacy_*_fixture.bin` outside the 32-bit i386 container.
- No third-party library may be added to the game binary.
- Follow repository Allman formatting, four-space indentation, meaningful variable names, public API documentation, and class-member documentation rules.
- Run `cd src && make format`, inspect formatter output, and retain it only when it does not introduce unrelated line-ending churn.
- Do not connect to a live server; all boot and smoke verification is local.

---

### Task 1: Shared Text Contract and Reproducible Census

**Files:**
- Create: `src/text_view.h`
- Create: `src/tests/text_view_tests.cpp`
- Create: `tools/string_view_census.py`
- Create: `tools/string_view_census_tests.py`
- Create: `docs/superpowers/string-view-exceptions.md`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces: `rots::text::truncate_at_null(std::string_view) noexcept -> std::string_view`.
- Produces: `python3 tools/string_view_census.py [--check]`, which inventories function parameters, scalar constants, and lookup tables that still expose candidate C-string or `const std::string&` types.
- Produces: an exception document keyed by normalized declaration and a specific contract category.

- [ ] **Step 1: Capture the baseline before changing source**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 --output-on-failure
rg -n "\\bconst char\\s*\\*|\\bconst std::string\\s*&" src --glob '*.{h,cpp}' --glob '!src/tests/**' > /tmp/string-view-baseline.txt
```

Expected: build succeeds; all currently discovered tests pass; `/tmp/string-view-baseline.txt` records the starting candidate surface without changing the repository.

- [ ] **Step 2: Add failing helper tests**

Add `src/tests/text_view_tests.cpp` with these cases:

```cpp
#include "text_view.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

TEST(TextView, PreservesOrdinaryBoundedText)
{
    const std::array<char, 5> storage { 'h', 'e', 'l', 'l', 'o' };
    EXPECT_EQ(rots::text::truncate_at_null(storage), "hello");
}

TEST(TextView, StopsAtFirstEmbeddedNull)
{
    constexpr std::string_view text("alpha\0omega", 11);
    EXPECT_EQ(rots::text::truncate_at_null(text), "alpha");
}

TEST(TextView, PreservesEmptyView)
{
    EXPECT_TRUE(rots::text::truncate_at_null({}).empty());
}
```

Add `tests/text_view_tests.cpp` to `ROTS_TEST_SOURCES` in `src/CMakeLists.txt`.

- [ ] **Step 3: Run the focused test and verify the red state**

Run:

```bash
cmake --build --preset macos-arm64 -j4
```

Expected: compilation fails because `text_view.h` and `rots::text::truncate_at_null` do not yet exist.

- [ ] **Step 4: Implement the shared helper**

Create `src/text_view.h`:

```cpp
#ifndef TEXT_VIEW_H
#define TEXT_VIEW_H

#include <string_view>

namespace rots::text {

/// Returns the textual prefix ending before the first null character.
[[nodiscard]] constexpr std::string_view truncate_at_null(std::string_view text) noexcept
{
    return text.substr(0, text.find('\0'));
}

} // namespace rots::text

#endif // TEXT_VIEW_H
```

- [ ] **Step 5: Add the census tool tests before the tool**

In `tools/string_view_census_tests.py`, use `tempfile.TemporaryDirectory` to create one header containing:

```cpp
void eligible(const char* text);
void eligible_string(const std::string& text);
void mutable_buffer(char* text);
constexpr const char* greeting = "hello";
```

Assert that report mode finds the first, second, and fourth declarations but not the mutable buffer. Assert that `--check` fails for an unclassified finding and succeeds when the normalized declaration appears in the supplied exception fixture with one of these reasons: `nullable-state`, `retains-storage`, `binary-data`, `printf-varargs`, `c-boundary`, `abi-layout`, or `sentinel-table`.

- [ ] **Step 6: Implement census report and check modes**

Implement `tools/string_view_census.py` with `argparse`, `pathlib`, and `re`. It must:

```python
CANDIDATE_PATTERNS = (
    re.compile(r"\bconst\s+char\s*\*"),
    re.compile(r"\bconst\s+std::string\s*&"),
)
ALLOWED_REASONS = {
    "nullable-state",
    "retains-storage",
    "binary-data",
    "printf-varargs",
    "c-boundary",
    "abi-layout",
    "sentinel-table",
}
```

Collapse multiline declarations before comparison, report path and source line, ignore comments and `src/tests/`, and make `--check` exit nonzero for every finding that is neither migrated nor listed with a permitted reason in `docs/superpowers/string-view-exceptions.md`.

- [ ] **Step 7: Verify helper and census tests**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^TextView\.' --output-on-failure
python3 -m unittest tools/string_view_census_tests.py
python3 tools/string_view_census.py
```

Expected: three `TextView` tests and all Python tests pass; report mode prints the current candidate inventory.

- [ ] **Step 8: Seed only proven exceptions**

Populate `docs/superpowers/string-view-exceptions.md` with the normalized declarations already proven to be external C, mutable/retained, nullable-state, binary, varargs, ABI, or sentinel contracts. Do not classify an entry merely because migration is inconvenient. Leave eligible findings for later tasks, so `--check` is not expected to pass yet.

- [ ] **Step 9: Commit the foundation**

```bash
git add src/text_view.h src/tests/text_view_tests.cpp src/CMakeLists.txt tools/string_view_census.py tools/string_view_census_tests.py docs/superpowers/string-view-exceptions.md
git diff --cached --check
git commit -m "refactor: define bounded text-view contract"
```

---

### Task 2: Core Text Utilities and Read-Only Constants

**Files:**
- Modify: `src/utils.h`
- Modify: `src/utility.cpp`
- Modify: `src/char_utils.h`
- Modify: `src/char_utils.cpp`
- Modify: `src/handler.h`
- Modify: `src/handler.cpp`
- Modify: `src/color.h`
- Modify: `src/color.cpp`
- Modify: `src/safe_template.h`
- Modify: `src/safe_template.cpp`
- Modify: `src/consts.cpp`
- Modify: headers declaring converted tables, especially `src/structs.h`, `src/spells.h`, and `src/interpre.h`
- Modify: `src/tests/char_utils_tests.cpp`
- Modify: `src/tests/color_tests.cpp`
- Modify: `src/tests/safe_template_tests.cpp`
- Create: `src/tests/string_view_utility_tests.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `rots::text::truncate_at_null` from Task 1.
- Produces: view-based comparison, search, color, and template helpers used by all later tasks.
- Produces: `constexpr std::string_view` scalar constants and `std::array<std::string_view, N>` tables when no null sentinel or ABI contract exists.

- [ ] **Step 1: Add characterization tests for foundational APIs**

Test ordinary strings, five-byte non-terminated arrays, empty views, and `"name\0ignored"` slices for `str_cmp`, `strn_cmp`, `isname`, the `char_utils` equality/contains helpers, color lookup helpers, and `safe_template::substitute`. Add compile-time assertions that migrated table elements are `std::string_view`:

```cpp
static_assert(std::same_as<std::remove_cvref_t<decltype(dirs[0])>, std::string_view>);
```

Where a table uses a null sentinel, test its existing terminal condition and retain it as a documented exception rather than replacing null with an empty string.

- [ ] **Step 2: Verify the tests fail against pointer/reference signatures**

Run:

```bash
cmake --build --preset macos-arm64 -j4
```

Expected: compilation fails at bounded-view calls or type assertions.

- [ ] **Step 3: Change foundational signatures in place**

Use this implementation pattern at the first semantic consumer:

```cpp
int str_cmp(std::string_view first, std::string_view second)
{
    first = rots::text::truncate_at_null(first);
    second = rots::text::truncate_at_null(second);
    // Preserve the existing case-insensitive ordering over the bounded ranges.
}
```

Use indices or iterators rather than calling `strlen`, `strcmp`, `strstr`, or reading beyond `end()`. Preserve exact legacy case folding and prefix semantics; do not replace project-specific comparisons with locale-sensitive alternatives.

- [ ] **Step 4: Convert safe constants and tables with their consumers**

Use:

```cpp
inline constexpr std::string_view constant_name = "text";
constexpr std::array<std::string_view, entry_count> names { "first", "second" };
```

Update formatting and comparison callers to consume views directly. At an unchanged C boundary, materialize or use an owned string explicitly. Keep persisted struct fields and null-sentinel tables unchanged and add their normalized declarations to the exception document with the correct reason.

- [ ] **Step 5: Remove the private logging helper duplicate**

Replace `utility.cpp`'s file-local `truncate_at_null` with `rots::text::truncate_at_null` and include `text_view.h`. Re-run `UtilityFormat` tests to ensure the existing log/mudlog behavior is unchanged.

- [ ] **Step 6: Run focused tests and census report**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^(TextView|StringViewUtility|CharUtils|Color|SafeTemplate|UtilityFormat)\.' --output-on-failure
python3 tools/string_view_census.py
```

Expected: focused tests pass; candidate count decreases; no new unclassified pointer signature is introduced.

- [ ] **Step 7: Commit core utilities and constants**

```bash
git add src/utils.h src/utility.cpp src/char_utils.h src/char_utils.cpp src/handler.h src/handler.cpp src/color.h src/color.cpp src/safe_template.h src/safe_template.cpp src/consts.cpp src/structs.h src/spells.h src/interpre.h src/tests src/CMakeLists.txt docs/superpowers/string-view-exceptions.md
git diff --cached --check
git commit -m "refactor: make core text utilities view-based"
```

---

### Task 3: Basic Communication and Descriptor Writes

**Files:**
- Modify: `src/comm.h`
- Modify: `src/comm.cpp`
- Modify: `src/tests/comm_output_tests.cpp`
- Modify: `src/tests/utility_format_tests.cpp`

**Interfaces:**
- Consumes: Task 1 normalization and Task 2 text utilities.
- Produces: `send_to_all`, `send_to_except`, `send_to_room`, `send_to_room_except`, `send_to_room_except_two`, `send_to_outdoor`, `send_to_sector`, `perform_to_all`, and `write_to_descriptor` with `std::string_view` message parameters.
- Retains: `vsend_to_char(char_data*, const char* format, ...)` as a documented `printf-varargs` boundary.

- [ ] **Step 1: Add bounded communication tests**

For every broadcast family, construct a descriptor fixture and pass:

```cpp
const std::array<char, 8> storage { 'm', 'e', 's', 's', 'a', 'g', 'e', 'X' };
const std::string_view message(storage.data(), 7);
```

Assert exact output. Add embedded-null cases and verify the suffix is absent. Add a signature test that a `std::string_view` overload is selected without `.data()`.

- [ ] **Step 2: Verify the red state**

Run:

```bash
cmake --build --preset macos-arm64 -j4
```

Expected: compilation fails for communication functions that still require `const char*` or `char*`.

- [ ] **Step 3: Convert signatures and length-aware implementations**

Change declarations and definitions in place:

```cpp
void send_to_all(std::string_view message);
void send_to_room(std::string_view message, int room);
int write_to_descriptor(SocketType descriptor, std::string_view text);
```

Normalize at the first function that reads the message. Forward views unchanged through pure fan-out functions. For socket writes, use the known view length and preserve partial-write/error behavior; do not call `strlen` or assume termination.

- [ ] **Step 4: Remove nullable compatibility overloads**

Remove the two `send_to_char(const char*, ...)` overloads added by the earlier narrow pass. Rewrite nullable callers as explicit branches preserving no-op behavior. Ordinary strings and literals call the view overload directly.

- [ ] **Step 5: Remove communication adapters**

Remove `.c_str()` and `.data()` only when used solely to satisfy one of the converted communication signatures. Do not remove adapters passed into unchanged printf-varargs, filesystem, cryptographic, or protocol C boundaries.

- [ ] **Step 6: Run focused communication tests**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^(CommOutput|UtilityFormat)\.' --output-on-failure
```

Expected: all focused tests pass with byte-identical output.

- [ ] **Step 7: Commit basic communication migration**

```bash
git add src/comm.h src/comm.cpp src/tests/comm_output_tests.cpp src/tests/utility_format_tests.cpp
git diff --cached --check
git commit -m "refactor: make communication broadcasts length-aware"
```

---

### Task 4: `act`, Queues, Paging, and Protocol-Facing Communication

**Files:**
- Modify: `src/comm.h`
- Modify: `src/comm.cpp`
- Modify: `src/protocol.h`
- Modify: `src/protocol.cpp`
- Modify: `src/tests/comm_act_tests.cpp`
- Modify: `src/tests/comm_output_tests.cpp`
- Modify: `src/tests/protocol_tests.cpp`

**Interfaces:**
- Produces: `act(std::string_view, ...)` and a bounded internal token-expansion path.
- Produces: view-accepting queue/page entry points that copy when storage survives the call.
- Produces: view-based protocol inputs only where protocol state does not retain borrowed storage.

- [ ] **Step 1: Add act and retention characterization**

Add `act` tests for a non-terminated format slice, embedded null, dollar-token expansion at the final byte, empty input, and temporary `std::format` input. Add queue/page tests that destroy or overwrite the caller's storage immediately after the call and confirm queued/paged output remains valid.

- [ ] **Step 2: Add protocol boundary tests**

For each protocol function selected for conversion, pass a bounded non-terminated value and an embedded-null value. Assert exact telnet/MSDP/MXP bytes and confirm retained protocol variables own their stored value.

- [ ] **Step 3: Verify tests fail before implementation**

Run:

```bash
cmake --build --preset macos-arm64 -j4
```

Expected: bounded inputs fail to compile or retention tests expose pointer-based assumptions.

- [ ] **Step 4: Rewrite act parsing over bounded ranges**

Change `act` and `convert_string` to accept views and iterate with indices/iterators. Replace pointer walking that depends on a terminal null with explicit end checks. Preserve every `$` token, color, visibility, spam, and recipient rule pinned by `CommAct`.

- [ ] **Step 5: Make retained communication storage explicit**

For `write_to_q`, text-block pool insertion, and paging, copy normalized text into the existing owning allocation before returning. Do not store `view.data()`. If `page_string` ownership differs under `keep_internal`, retain separate documented entry points rather than obscuring ownership behind one borrowed signature.

- [ ] **Step 6: Convert eligible protocol functions**

Convert functions such as value sanitization and immediate MSDP/MXP sends when they consume text synchronously. Keep `ProtocolInput` mutable, and keep pointer-returning/tag-buffer APIs or retained C fields as documented exceptions unless the implementation copies before returning.

- [ ] **Step 7: Run focused tests**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^(ActTokenExpansion|CommOutput|Protocol)\.' --output-on-failure
```

Expected: all focused tests pass, including lifetime cases.

- [ ] **Step 8: Commit expanded communication migration**

```bash
git add src/comm.h src/comm.cpp src/protocol.h src/protocol.cpp src/tests/comm_act_tests.cpp src/tests/comm_output_tests.cpp src/tests/protocol_tests.cpp docs/superpowers/string-view-exceptions.md
git diff --cached --check
git commit -m "refactor: bound expanded and retained communication text"
```

---

### Task 5: Gameplay Lookups, Parsers, and Command Helpers

**Files:**
- Modify: `src/handler.h`, `src/handler.cpp`
- Modify: `src/interpre.h`, `src/interpre.cpp`
- Modify: `src/spells.h`, `src/spell_pa.cpp`
- Modify: `src/big_brother.h`, `src/big_brother.cpp`
- Modify: eligible declarations/definitions in `src/act_*.cpp`, `src/fight.cpp`, `src/mage.cpp`, `src/ranger.cpp`, `src/clerics.cpp`, `src/mystic.cpp`, `src/spec_pro.cpp`, `src/shop.cpp`, `src/objsave.cpp`, `src/limits.cpp`, `src/weather.cpp`, and `src/zone.cpp`
- Modify: focused tests in `src/tests/char_utils_tests.cpp`, `src/tests/act_format_tests.cpp`, `src/tests/characterization_combat_tests.cpp`, `src/tests/mage_tests.cpp`, `src/tests/spec_pro_tests.cpp`, and `src/tests/zone_tests.cpp`

**Interfaces:**
- Produces: view-based read-only names, keywords, descriptions, spell names, and search fragments.
- Retains: in-place command tokenizers and mutable ACMD argument buffers as mutable exceptions.

- [ ] **Step 1: Classify gameplay parameters before editing**

For each candidate, inspect its body and callers. Mark it eligible only if it neither writes through the parameter nor stores it. Add proven mutable functions such as `get_number(char**)`, `find_all_dots(char*)`, and in-place command splitting to the exception document rather than wrapping literals with unsafe casts.

- [ ] **Step 2: Add representative failing tests per behavior family**

Cover name matching, keyword lookup, spell lookup, combat message lookup, shop/template selection, and zone lookup using bounded slices and embedded nulls. Preserve current case folding, abbreviation, numbered-name, and visibility behavior.

- [ ] **Step 3: Change eligible signatures in place**

Use `std::string_view` throughout read-only chains. Replace `strchr`, `strstr`, `strcmp`, and pointer arithmetic with bounded view operations or the Task 2 helpers. Materialize a string only when an unchanged mutable parser or C boundary requires it.

- [ ] **Step 4: Remove obsolete mutable literal adapters**

Delete `mutable_arg` uses only where the complete callee chain has become read-only. Keep the class and remaining uses until the residual census proves every use can be removed safely.

- [ ] **Step 5: Run gameplay-focused tests**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^(CharUtils|Act|CharacterizationCombat|Mage|SpecPro|Zone)\.' --output-on-failure
```

Expected: focused suites pass with unchanged behavior.

- [ ] **Step 6: Commit gameplay migration**

```bash
git add src/handler.h src/handler.cpp src/interpre.h src/interpre.cpp src/spells.h src/spell_pa.cpp src/big_brother.h src/big_brother.cpp src/act_*.cpp src/fight.cpp src/mage.cpp src/ranger.cpp src/clerics.cpp src/mystic.cpp src/spec_pro.cpp src/shop.cpp src/objsave.cpp src/limits.cpp src/weather.cpp src/zone.cpp src/tests docs/superpowers/string-view-exceptions.md
git diff --cached --check
git commit -m "refactor: borrow gameplay lookup text as views"
```

---

### Task 6: Account Management and Presentation

**Files:**
- Modify: `src/account_cache.h`, `src/account_cache.cpp`
- Modify: `src/account_management_assets.h`, `src/account_management_assets.cpp`
- Modify: `src/account_management_identity.h`, `src/account_management_identity.cpp`
- Modify: `src/account_management_migration.h`, `src/account_management_migration.cpp`
- Modify: `src/account_management_presentation.h`, `src/account_management_presentation.cpp`
- Modify: `src/account_management_storage.h`, `src/account_management_storage.cpp`
- Modify: `src/account_management.h`, `src/account_management.cpp`
- Modify: `src/player_file_finalize.h`, `src/player_file_finalize.cpp`
- Modify: `src/tests/account_cache_tests.cpp`
- Modify: `src/tests/account_management_tests.cpp`
- Modify: `src/tests/player_finalize_tests.cpp`

**Interfaces:**
- Produces: view-based borrowed root directories, account names, emails, passwords, character names, verification values, administrative reasons, and presentation inputs.
- Keeps output parameters and owning `AccountData` fields unchanged.

- [ ] **Step 1: Add bounded identity and path tests**

Add cases for each public family using non-terminated account/email/name slices and embedded-null suffixes. Verify normalized identity, validation errors, cache keys, generated paths, authentication, migration selection, and presentation bytes match the equivalent terminated input.

- [ ] **Step 2: Verify the signature red state**

Run:

```bash
cmake --build --preset macos-arm64 -j4
```

Expected: bounded callers fail where public APIs still require `const std::string&`.

- [ ] **Step 3: Convert public headers and implementations together**

Change borrowed parameters to views. Normalize before validation, comparison, hashing, path construction, or cryptographic use. Copy into owning account fields explicitly:

```cpp
account->account_name.assign(account_name);
```

At password hashing or OS/file boundaries, create one normalized owner and pass its terminated representation only to that boundary.

- [ ] **Step 4: Preserve cache ownership**

Build cache keys as owning `std::string` values before insertion. Do not store root/name views in cache keys, callbacks, or account objects.

- [ ] **Step 5: Run account-focused tests and smoke test**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^(Account|PlayerFinalize|InterpreAccountMenu)\.' --output-on-failure
make smoke-account
```

Expected: focused tests and the separate proxy-backed account smoke flow pass.

- [ ] **Step 6: Commit account migration**

```bash
git add src/account_* src/player_file_finalize.h src/player_file_finalize.cpp src/tests/account_cache_tests.cpp src/tests/account_management_tests.cpp src/tests/player_finalize_tests.cpp src/tests/interpre_account_menu_tests.cpp docs/superpowers/string-view-exceptions.md
git diff --cached --check
git commit -m "refactor: accept bounded account text inputs"
```

---

### Task 7: JSON, Persistence, and Binary Separation

**Files:**
- Modify: `src/json_utils.h`, `src/json_utils.cpp`
- Modify: `src/character_json.h`, `src/character_json.cpp`
- Modify: `src/objects_json.h`, `src/objects_json.cpp`
- Modify: `src/exploits_json.h`, `src/exploits_json.cpp`
- Modify: `src/boards.h`, `src/boards.cpp`
- Modify: `src/mail.h`, `src/mail.cpp`
- Modify: `src/pkill.h`, `src/pkill.cpp`
- Modify: `src/db.h`, `src/db.cpp`
- Modify: `src/save_benchmark.h`, `src/save_benchmark.cpp`
- Modify: matching JSON and round-trip tests under `src/tests/`

**Interfaces:**
- Produces: view-based textual JSON inputs and borrowed labels/titles.
- Retains full-byte semantics for `*_from_binary`, `*_to_binary`, fixture decoding, and legacy record payloads.

- [ ] **Step 1: Separate textual and binary candidates in the census**

Classify JSON documents, keys, labels, and escaped values as textual. Classify serialized byte buffers and legacy fixture payloads as `binary-data` exceptions even when their current C++ type is `std::string`.

- [ ] **Step 2: Add textual bounded-input tests**

For every deserializer family, pass a non-terminated JSON slice and an embedded-null suffix. Assert successful parsing of the prefix or the same error as an equivalent terminated prefix. Add escape/append tests for bounded values.

- [ ] **Step 3: Add binary embedded-null tests**

For each binary decoder touched by signature review, construct a payload containing internal zero bytes and assert the entire declared length remains visible. These tests prevent accidental use of `truncate_at_null` in binary code.

- [ ] **Step 4: Convert textual APIs only**

Change JSON readers and textual helpers to `std::string_view`. Update reader cursors to operate over `[begin, end)` without requiring a sentinel. Preserve owning output strings and error output parameters.

- [ ] **Step 5: Keep binary contracts explicit**

Retain `const std::string&` where it communicates owning/repeatable binary storage, or change to a clearly documented full-byte `std::span<const std::byte>` only if all callers and tests can migrate within this task. Do not apply textual first-null normalization to binary payloads.

- [ ] **Step 6: Run persistence tests**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^(Json|CharacterJson|ObjectsJson|Exploits|BoardsJson|MailJson|PkillJson|CrimeJson|DbSave|PodPersistence|SaveBenchmark)\.' --output-on-failure
```

Expected: textual bounded cases and binary full-byte cases pass; no golden changes occur.

- [ ] **Step 7: Commit JSON and persistence migration**

```bash
git add src/json_utils.* src/character_json.* src/objects_json.* src/exploits_json.* src/boards.* src/mail.* src/pkill.* src/db.* src/save_benchmark.* src/tests docs/superpowers/string-view-exceptions.md
git diff --cached --check
git commit -m "refactor: separate textual views from binary storage"
```

---

### Task 8: Filesystem, Conversion, Cryptography, and Remaining C Boundaries

**Files:**
- Modify: `src/convert_exploits.h`, `src/convert_exploits.cpp`
- Modify: `src/convert_plrobjs.h`, `src/convert_plrobjs.cpp`
- Modify: `src/legacy_salvage.h`
- Modify: `src/mob_csv_extract.h`, `src/mob_csv_extract.cpp`
- Modify: `src/platform_compat.h`, `src/utility.cpp`
- Modify: `src/rots_crypt.h`, `src/rots_crypt.cpp`
- Modify: corresponding tests in `src/tests/`

**Interfaces:**
- Produces: view-based internal path and text inputs.
- Retains: explicit terminated pointers at POSIX/Windows/libc/crypt boundaries and printf-format parameters.

- [ ] **Step 1: Add boundary tests**

Test non-terminated and embedded-null paths at the public C++ wrapper level using temporary directories. Verify the prefix path is used. Test password/key slices at the cryptographic wrapper and preserve existing hash/verification results.

- [ ] **Step 2: Convert C++ wrappers and materialize once**

Normalize each textual view, then create one owner adjacent to the boundary:

```cpp
const std::string path_owner(rots::text::truncate_at_null(path));
const int result = rots_remove(path_owner.c_str());
```

Do not propagate `c_str()` back through internal helpers. Keep `rots_asprintf(char**, const char*, ...)` and other printf templates pointer-based with `printf-varargs` documentation.

- [ ] **Step 3: Preserve nullable and platform contracts**

Where operating-system or crypt APIs assign meaning to null, retain the pointer signature and classify it as `nullable-state` or `c-boundary`. Do not translate null to an empty view unless current behavior already does so.

- [ ] **Step 4: Run converter and boundary tests**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^(Convert|Legacy|MobCsv|RotsAsprintf|RotsCrypt)\.' --output-on-failure
```

Expected: focused tests pass and no runtime data outside test temporary directories changes.

- [ ] **Step 5: Commit boundary localization**

```bash
git add src/convert_exploits.* src/convert_plrobjs.* src/legacy_salvage.h src/mob_csv_extract.* src/platform_compat.h src/utility.cpp src/rots_crypt.* src/tests docs/superpowers/string-view-exceptions.md
git diff --cached --check
git commit -m "refactor: localize null-terminated text boundaries"
```

---

### Task 9: Residual Constants, Adapters, and Enforced Exception Census

**Files:**
- Modify: all remaining candidate production files reported by `tools/string_view_census.py`
- Modify: `tools/string_view_census.py`
- Modify: `tools/string_view_census_tests.py`
- Modify: `docs/superpowers/string-view-exceptions.md`
- Modify: `docs/BUILD.md`
- Modify: `AGENTS.md`

**Interfaces:**
- Produces: a zero-unclassified-candidate `python3 tools/string_view_census.py --check` gate.
- Produces: documented rules for new read-only text APIs and justified exceptions.

- [ ] **Step 1: Run the residual reports**

Run:

```bash
python3 tools/string_view_census.py
rg -n -U "(?:send_to_|write_to_|act|log|mudlog|str_cmp|isname)[^;]*\.(?:c_str|data)\(\)" src --glob '*.{h,cpp}'
rg -n "std::format\(\"[^\"]*\"\)" src --glob '*.cpp' --glob '!src/tests/**'
```

Expected: the first command lists remaining candidates; adapter searches identify only genuine boundaries or unnecessary adapters/literal-only formatting.

- [ ] **Step 2: Resolve each remaining eligible declaration**

Convert eligible scalar constants, tables, function declarations, definitions, and callers. Remove literal-only `std::format` calls encountered in the touched paths. Do not broaden this step into an unrelated formatting rewrite.

- [ ] **Step 3: Complete exception documentation**

For every remaining candidate, record its normalized declaration, reason category, owner file, and one-sentence contract explanation. Reject generic explanations. Examples:

```markdown
- `src/platform_compat.h | int rots_asprintf(char** out, const char* format, ...)`
  - Reason: `printf-varargs`
  - Contract: libc-compatible format strings must be null-terminated for `vsnprintf`.
```

- [ ] **Step 4: Make check mode authoritative**

Run Python unit tests, then require:

```bash
python3 -m unittest tools/string_view_census_tests.py
python3 tools/string_view_census.py --check
```

Expected: both commands exit zero; adding an unclassified fixture declaration makes the unit test observe a nonzero check result.

- [ ] **Step 5: Update living guidance**

Add the type policy, first-null rule, binary exception, lifetime rule, and census command to `docs/BUILD.md` and repository `AGENTS.md`. Keep `CLAUDE.md` as a shim to `AGENTS.md`; do not duplicate the policy there.

- [ ] **Step 6: Run the full native suite before committing the sweep**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 --output-on-failure
git -c core.whitespace=cr-at-eol diff --check master..HEAD
```

Expected: build succeeds, all discovered tests pass, and no whitespace errors are reported.

- [ ] **Step 7: Commit residual enforcement**

```bash
git add src tools/string_view_census.py tools/string_view_census_tests.py docs/superpowers/string-view-exceptions.md docs/BUILD.md AGENTS.md
git diff --cached --check
git commit -m "refactor: enforce repository string-view boundaries"
```

---

### Task 10: Performance, Platform, Golden, and Completion Gates

**Files:**
- Modify only if measurements expose a regression: the specific implementation and its focused test from Tasks 2-8
- Modify: `docs/superpowers/plans/2026-07-13-repository-string-view-first-pass.md` to append measured results and exact final census counts

**Interfaces:**
- Consumes: all migrated layers and the zero-unclassified census gate.
- Produces: verified performance and portability evidence for completion.

- [x] **Step 1: Measure representative paths**

Measure at least prompt composition, a room/all broadcast to multiple descriptors, name/keyword lookup, and JSON parsing. Use the same optimized compiler, fixtures, iteration counts, and quiet host for baseline and migrated commits. Record median nanoseconds per operation and allocation counts when available. Reject a statistically repeatable regression greater than 10% in an obviously hot path unless the user explicitly accepts it.

- [x] **Step 2: Correct measured regressions test-first**

If a hot path regresses, add or extend a focused characterization test, then use a reserved owner, `std::format_to`, `std::to_chars`, or a single boundary materialization as appropriate. Document the allocation/readability trade-off at the decision point and repeat the identical measurement.

- [x] **Step 3: Run native build and full tests**

Run:

```bash
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 --output-on-failure
```

Expected: build succeeds and every discovered test passes, with platform-gated skips only.

- [x] **Step 4: Run local boot golden outside socket-restricted sandboxing**

Run:

```bash
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
```

Expected: `boot log matches golden`. If sandboxed bind returns `Operation not permitted`, rerun the same local command with sandbox restriction lifted; do not connect to production.

- [x] **Step 5: Run Linux x64 and i386 gates**

Run:

```bash
docker compose run --rm rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
docker compose run --rm rots bash -lc 'cd /rots && make configure && make build && make test'
```

Expected: both builds and test suites pass. Do not set `UPDATE_GOLDENS=1`.

- [x] **Step 6: Run final static and repository checks**

Run:

```bash
python3 tools/string_view_census.py --check
python3 -m unittest tools/string_view_census_tests.py
git -c core.whitespace=cr-at-eol diff --check master..HEAD
git status --short --branch
git diff --name-only origin/master..HEAD -- 'src/tests/goldens/legacy_*_fixture.bin'
```

Expected: census and Python tests pass; the CRLF-aware full-range diff check is silent; working tree
is clean; legacy fixture command prints nothing.

- [x] **Step 7: Append evidence and commit the results**

Append exact before/after census counts, benchmark results, test totals/skips, container results, boot result, formatter disposition, and fixture confirmation to this plan. Then run:

```bash
git add docs/superpowers/plans/2026-07-13-repository-string-view-first-pass.md
git diff --cached --check
git commit -m "docs: record string-view migration verification"
```

- [x] **Step 8: Request final code review**

Use `superpowers:requesting-code-review` against the complete commit range. Resolve correctness, lifetime, embedded-null, binary-data, and C-boundary findings before offering integration options through `superpowers:finishing-a-development-branch`.

#### Task 10 verification evidence (2026-07-13)

- Census: the initial raw pointer/reference search recorded 1,359 lines. The enforced census had
  506 candidates after Task 8, 83 classified exceptions after Task 9, and 84 after the measured
  `isname_nullable` terminated-pointer hot-path correction. Final review expanded file-scope
  constant detection and added constrained protocol compatibility adapters that preserve null
  behavior without exposing additional C-string declarations. The final ledger therefore remains
  at 84 classified exceptions with zero unclassified candidates. All 15 census-tool tests pass.
- Representative performance: an identical AppleClang Release `-O2` harness compared baseline
  `c808cb1` with migrated `8fbff01`, using observable checksums, two warmups, and 11 samples per
  path. Median prompt composition was 146.958 to 149.506 ns/op (+1.73%); a six-recipient room
  broadcast was 56.121 to 35.675 ns/op (-36.43%); name lookup was 18.811 to 25.525 ns/op
  (+35.69%); and JSON parsing was 238.221 to 225.354 ns/op (-5.40%). Allocation counts were not
  instrumented in this harness.
- Name-lookup correction: a focused harness with ten warmups, 11 samples, and 1,000,000 operations
  per sample confirmed that the nullable wrapper's four redundant scans produced a 33.715 to
  51.827 ns/op regression (+53.7%). The final direct-sentinel wrapper retained the bounded
  `std::string_view` overload and measured 36.028 to 34.913 ns/op (-3.1%) and 37.618 to 36.859
  ns/op (-2.0%) in simultaneous paired runs. Normal and macOS ASan `StringViewUtility.*` tests pass
  8/8.
- Portability and full suites: macOS arm64 passed all 1,207 discovered tests (1,132 executed passes,
  75 platform skips); Linux x64 passed all 1,207 (1,130 executed passes, 77 platform skips); the
  i386 CMake suite passed all 1,207 (1,201 executed passes, 6 platform skips); and the independent
  i386 legacy Makefile suite passed 1,171 tests with 6 skips. A Linux ASan/UBSan reproduction suite
  covering the three migration-boundary crashes passes 3/3, and a custom printf-format compile
  audit found and corrected every `std::string_view` object passed through the audited `%s`
  varargs boundaries.
- Runtime characterization: native macOS arm64, `rots64`, and shipping-ABI i386 boot checks each
  reported `boot log matches golden`. The Docker checks used the same local world data through an
  untracked Docker-visible copy; no live server was contacted.
- Repository integrity: no legacy binary fixture or other characterization golden changed. The
  repository formatter was not run as a final bulk rewrite because several legacy files retain
  intentional mixed line endings; changes stayed localized, and
  `git -c core.whitespace=cr-at-eol diff --check master..HEAD` is silent. Generated root-build state
  was restored after i386 verification, and temporary runtime data and binaries were removed from
  the worktree.
- Final-review corrections: both incremental JSON readers now own one first-null-truncated input
  copy, with an ASan regression test that reproduced the prior dangling-view use-after-free before
  the fix. All bounded protocol overloads remain available while constrained adapters preserve the
  11 historical null contracts and direct raw-array extents; ASan reproduced the former raw-array
  `strlen` over-read before that correction. The expanded census also migrated newly visible
  literal-backed file-scope constants in `config.cpp`, `comm.cpp`, `handler.cpp`, `interpre.cpp`,
  and `objsave.cpp`. Normal and ASan focused suites pass 70/70. Two paired optimized benchmark runs
  measured JSON at
  236.604 to 235.088 ns/op (-0.6%) and 234.642 to 235.583 ns/op (+0.4%), showing no material cost
  from the required ownership copy. The post-correction macOS suite passes all 1,216 discovered
  tests (1,141 executed passes and 75 platform skips), and Linux x64 passes all 1,216 (1,139
  executed passes and 77 platform skips). The post-correction shipping-ABI i386 suite also passes
  all 1,216 discovered tests (1,210 executed passes and 6 platform skips). Final review reported no
  critical, important, or minor findings and approved the branch for integration.
