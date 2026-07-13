# String-View Text Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make descriptor output, both `send_to_char` paths, `log`, and `mudlog` accept bounded `std::string_view` input while preserving legacy null-pointer and embedded-null behavior.

**Architecture:** Make `write_to_output(std::string_view, descriptor_data*)` the length-aware copy boundary and retain nullable `const char*` compatibility overloads only on `send_to_char`. Normalize views at their first null byte before output. Convert logging to explicit-length stderr writes, then remove obsolete `.c_str()`/`.data()` adapters only at direct calls to the converted APIs.

**Tech Stack:** C++20, GoogleTest, CMake presets, WebKit `clang-format` style.

## Global Constraints

- Preserve byte-for-byte player-visible and log output, including truncation at the first embedded null byte.
- Preserve `send_to_char(nullptr, ...)` as a no-op through `const char*` compatibility overloads.
- Do not retain a `std::string_view` or its data pointer after the receiving call returns.
- Preserve the existing small-buffer, large-buffer, and overflow-state policy.
- Use Allman braces, four-space indentation, mandatory braces, meaningful variable names, and comments for every class-scoped variable.
- Document every modified public production declaration with `///` documentation.
- Restrict caller cleanup to direct `write_to_output`, `send_to_char`, `log`, and `mudlog` arguments.
- Do not access a live server or alter runtime/world data.

---

### Task 1: Length-aware descriptor and player output

**Files:**
- Create: `src/tests/comm_output_tests.cpp`
- Modify: `src/CMakeLists.txt:220-285`
- Modify: `src/comm.h:12-70`
- Modify: `src/comm.cpp:1215-1255,2001-2029`

**Interfaces:**
- Produces: `void write_to_output(std::string_view text, descriptor_data* descriptor)`
- Produces: `void send_to_char(std::string_view message, char_data* character)`
- Produces: `void send_to_char(const char* message, char_data* character)`
- Produces: `void send_to_char(std::string_view message, int character_id)`
- Produces: `void send_to_char(const char* message, int character_id)`

- [ ] **Step 1: Add and run the null-pointer characterization test**

Add `tests/comm_output_tests.cpp` to `ROTS_TEST_SOURCES` in `src/CMakeLists.txt`. Create the test file with a capturing descriptor fixture and this existing-behavior test:

```cpp
#include "../comm.h"
#include "../structs.h"
#include "test_char_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

extern descriptor_data* descriptor_list;

namespace {

class ScopedDescriptorListReset {
public:
    ScopedDescriptorListReset()
        : previous_descriptor_list_(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorListReset()
    {
        descriptor_list = previous_descriptor_list_;
    }

    ScopedDescriptorListReset(const ScopedDescriptorListReset&) = delete;
    ScopedDescriptorListReset& operator=(const ScopedDescriptorListReset&) = delete;

private:
    // Restores the process-global descriptor chain after an isolated test.
    descriptor_data* previous_descriptor_list_;
};

void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0;
    descriptor.character = character;
}

struct ConnectedCharacterContext {
    // Receives messages through the character-pointer and character-ID APIs.
    char_data character {};
    // Captures queued output without opening a network connection.
    descriptor_data descriptor {};

    ConnectedCharacterContext()
    {
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.abs_number = 4207;
    }
};

} // namespace

TEST(CommOutput, SendToCharNullPointerRemainsANoOp)
{
    ConnectedCharacterContext context;

    send_to_char(static_cast<const char*>(nullptr), &context.character);

    EXPECT_STREQ(context.descriptor.output, "");
}
```

Run:

```bash
cd src
cmake --preset macos-arm64
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^CommOutput.SendToCharNullPointerRemainsANoOp$'
```

Expected: the characterization test passes before production signatures change.

- [ ] **Step 2: Add failing bounded-view tests**

Append tests that pass views which cannot be represented safely by the current `const char*` API:

```cpp
TEST(CommOutput, WriteToOutputAcceptsANonNullTerminatedSlice)
{
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, nullptr);
    const char storage[] = { 'x', 'v', 'i', 'e', 'w', 'y' };

    write_to_output(std::string_view(storage + 1, 4), &descriptor);

    EXPECT_STREQ(descriptor.output, "view");
    EXPECT_EQ(descriptor.bufptr, 4);
    EXPECT_EQ(descriptor.bufspace, SMALL_BUFSIZE - 5);
}

TEST(CommOutput, WriteToOutputTruncatesAtAnEmbeddedNull)
{
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, nullptr);
    const char storage[] = { 'o', 'k', '\0', 'n', 'o' };

    write_to_output(std::string_view(storage, sizeof(storage)), &descriptor);

    EXPECT_STREQ(descriptor.output, "ok");
    EXPECT_EQ(descriptor.bufptr, 2);
}

TEST(CommOutput, WriteToOutputPromotesToTheLargeBufferUsingViewLength)
{
    descriptor_data descriptor {};
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };
    reset_capturing_descriptor(descriptor, nullptr);
    std::memcpy(descriptor.small_outbuf, "pre", 4);
    descriptor.bufptr = 3;
    descriptor.bufspace = 2;

    write_to_output(std::string_view("view"), &descriptor);

    ASSERT_NE(descriptor.large_outbuf, nullptr);
    EXPECT_STREQ(descriptor.output, "preview");
    EXPECT_EQ(descriptor.bufptr, 7);
    EXPECT_EQ(descriptor.bufspace, LARGE_BUFSIZE - 8);
}

TEST(CommOutput, SendToCharAcceptsAViewForACharacterPointer)
{
    ConnectedCharacterContext context;
    const std::string storage = "prefix-message-suffix";

    send_to_char(std::string_view(storage).substr(7, 7), &context.character);

    EXPECT_STREQ(context.descriptor.output, "message");
}

TEST(CommOutput, SendToCharAcceptsAViewForACharacterId)
{
    ScopedDescriptorListReset descriptor_list_reset;
    ConnectedCharacterContext context;
    descriptor_list = &context.descriptor;
    const std::string storage = "prefix-message-suffix";

    send_to_char(std::string_view(storage).substr(7, 7), context.character.abs_number);

    EXPECT_STREQ(context.descriptor.output, "message");
}
```

Run:

```bash
cd src
cmake --build --preset macos-arm64 -j4
```

Expected: compilation fails because `std::string_view` cannot bind to the existing `const char*` parameters.

- [ ] **Step 3: Implement the length-aware boundary and overloads**

Add `<string_view>` to `comm.h`, document the five declarations listed under **Interfaces**, and replace `write_to_output` with a view-based implementation. The implementation must:

```cpp
text = text.substr(0, text.find('\0'));

if (descriptor->bufptr < 0)
{
    return;
}

const std::size_t text_size = text.size();
if (text_size <= static_cast<std::size_t>(descriptor->bufspace))
{
    // This output path runs for nearly every player message. Copy the known
    // view length directly to avoid rescanning it or allocating a temporary.
    std::memcpy(descriptor->output + descriptor->bufptr, text.data(), text_size);
    descriptor->bufptr += static_cast<int>(text_size);
    descriptor->bufspace -= static_cast<int>(text_size);
    descriptor->output[descriptor->bufptr] = '\0';
    return;
}
```

For large-buffer promotion, use the existing `bufptr` as the old payload length, check `text_size` against `LARGE_BUFSIZE - 1 - old_payload_size`, copy the old payload and new view with `memcpy`, terminate once, and recompute both counters. Keep `buf_switches`, `buf_overflows`, and pool ownership unchanged.

Implement each nullable pointer overload as:

```cpp
if (message == nullptr)
{
    return;
}
send_to_char(std::string_view(message), destination);
```

Implement each view overload with the existing character/connection guards and call `write_to_output(message, descriptor)` directly. Do not store the view.

- [ ] **Step 4: Run focused tests and commit**

Run:

```bash
cd src
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^CommOutput\.' --output-on-failure
```

Expected: all `CommOutput.*` tests pass.

Commit:

```bash
git add src/CMakeLists.txt src/comm.h src/comm.cpp src/tests/comm_output_tests.cpp
git commit -m "refactor: make player output length-aware"
```

---

### Task 2: Length-aware logging

**Files:**
- Modify: `src/utils.h:10-90`
- Modify: `src/utility.cpp:20-50,1203-1272`
- Modify: `src/tests/utility_format_tests.cpp`

**Interfaces:**
- Consumes: `send_to_char(std::string_view, char_data*)` from Task 1
- Produces: `void log(std::string_view message)`
- Produces: `void mudlog(std::string_view message, char type, sh_int level, byte file)`

- [ ] **Step 1: Add failing sliced-view logging tests**

Add `<string_view>` and these tests to `utility_format_tests.cpp`:

```cpp
TEST(UtilityFormat, MudlogAcceptsANonNullTerminatedSlice)
{
    ScopedDescriptorListReset descriptor_list_reset;
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;
    const std::string storage = "prefix-message-suffix";

    mudlog(std::string_view(storage).substr(7, 7), BRF, LEVEL_GOD, FALSE);

    EXPECT_STREQ(listener.descriptor.output, "[ message ]\n\r");
}

TEST(UtilityFormat, MudlogTruncatesAViewAtAnEmbeddedNull)
{
    ScopedDescriptorListReset descriptor_list_reset;
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;
    const char storage[] = { 'o', 'k', '\0', 'n', 'o' };

    mudlog(std::string_view(storage, sizeof(storage)), BRF, LEVEL_GOD, FALSE);

    EXPECT_STREQ(listener.descriptor.output, "[ ok ]\n\r");
}

TEST(UtilityFormat, LogWritesOnlyTheSelectedViewToStderr)
{
    const std::string storage = "prefix-message-suffix";
    testing::internal::CaptureStderr();

    log(std::string_view(storage).substr(7, 7));

    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(output.ends_with(" :: message\n"));
    EXPECT_EQ(output.find("suffix"), std::string::npos);
}
```

Run:

```bash
cd src
cmake --build --preset macos-arm64 -j4
```

Expected: compilation fails because the existing logging declarations require `const char*`.

- [ ] **Step 2: Implement view-based logging**

Add `<string_view>` to `utils.h`, change and document both declarations, and add this file-local helper in `utility.cpp`:

```cpp
static std::string_view truncate_at_null(std::string_view message)
{
    return message.substr(0, message.find('\0'));
}
```

Normalize at the beginning of both logging functions. Preserve the timestamp and type prefixes, but replace `%s` body formatting with explicit-length writes:

```cpp
std::fprintf(stderr, "%-19.19s :: ", timestamp);
std::fwrite(message.data(), sizeof(char), message.size(), stderr);
std::fputc('\n', stderr);
```

Use the corresponding existing `"%d, %-19.19s :: "` prefix in `mudlog`. Continue building the listener message with `std::format("[ {} ]\n\r", message)` and pass the resulting `std::string` directly to `send_to_char`.

Change `log_death_trap` to pass its `std::string` directly to `mudlog`.

- [ ] **Step 3: Run focused tests and commit**

Run:

```bash
cd src
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R '^UtilityFormat\.' --output-on-failure
```

Expected: all `UtilityFormat.*` tests pass.

Commit:

```bash
git add src/utils.h src/utility.cpp src/tests/utility_format_tests.cpp
git commit -m "refactor: accept bounded views in logging"
```

---

### Task 3: Remove obsolete direct-call adapters

**Files:**
- Modify direct call sites, as applicable, in: `src/act_comm.cpp`, `src/act_info.cpp`, `src/act_move.cpp`, `src/act_obj1.cpp`, `src/act_obj2.cpp`, `src/act_othe.cpp`, `src/act_wiz.cpp`, `src/ban.cpp`, `src/boards.cpp`, `src/clerics.cpp`, `src/color.cpp`, `src/comm.cpp`, `src/db.cpp`, `src/graph.cpp`, `src/handler.cpp`, `src/interpre.cpp`, `src/mage.cpp`, `src/modify.cpp`, `src/mystic.cpp`, `src/objsave.cpp`, `src/olog_hai.cpp`, `src/profs.cpp`, `src/ranger.cpp`, `src/savebench.cpp`, `src/shapemdl.cpp`, `src/shapemob.cpp`, `src/shapeobj.cpp`, `src/shaperom.cpp`, `src/shapescript.cpp`, `src/shapezon.cpp`, `src/shop.cpp`, `src/spec_pro.cpp`, `src/utility.cpp`, and `src/weather.cpp`.

**Interfaces:**
- Consumes: all view-based APIs from Tasks 1 and 2
- Produces: direct `std::string` and `std::format` arguments without C-string adapters

- [ ] **Step 1: Inventory only direct adapters**

Run:

```bash
rg -n -U '\b(send_to_char|mudlog|log)\([^;]{0,500}?\.(c_str|data)\(\)' src --glob '*.cpp'
rg -n 'const_cast<char\*>\([^\n]*(c_str|data)\(\)\)' src --glob '*.cpp'
```

For each match, confirm syntactically that `.c_str()`, `.data()`, or `const_cast<char*>` applies to the first argument of `send_to_char`, `mudlog`, or `log`. Do not change nested adapters belonging to `std::format`, `strcpy`, protocol functions, page functions, or other C-string APIs.

- [ ] **Step 2: Apply the mechanical cleanup in source-family batches**

Use these exact transformations only:

```cpp
send_to_char(message.c_str(), character);
// becomes
send_to_char(message, character);

send_to_char(std::format("...", values...).c_str(), character);
// becomes
send_to_char(std::format("...", values...), character);

mudlog(message.data(), type, level, file);
// becomes
mudlog(message, type, level, file);

log(const_cast<char*>(message.c_str()));
// becomes
log(message);
```

Preserve `.c_str()` and `.data()` on any nested argument consumed by another function. After each source-family batch, build before continuing:

```bash
cd src
cmake --build --preset macos-arm64 -j4
```

Expected: the server and test executable compile after every batch.

- [ ] **Step 3: Prove no obsolete direct adapter remains**

Repeat the inventory commands and manually inspect every residual match. Expected residuals are only false positives where the adapter belongs to a nested non-converted API. The following known casts must be gone:

```text
src/comm.cpp: mudlog(const_cast<char*>(std::format(...).c_str()), ...)
src/comm.cpp: mudlog(const_cast<char*>(buf.c_str()), ...)
src/mystic.cpp: log(const_cast<char*>(msg.c_str()))
```

- [ ] **Step 4: Commit the caller migration**

Run focused suites before committing:

```bash
cd src
ctest --preset macos-arm64 -R '^(CommOutput|UtilityFormat)\.' --output-on-failure
```

Use `git status --short` to identify which of the inventoried source files actually changed, then
stage those paths individually. Do not use `git add src` because that could capture concurrent
work. Commit the staged caller migration:

```bash
git commit -m "refactor: pass strings directly to text output"
```

---

### Task 4: Format and verify the complete change

**Files:**
- Modify: only formatting changes in files already touched by Tasks 1-3

**Interfaces:**
- Consumes: completed Tasks 1-3
- Produces: a warning-clean, test-clean C++20 build

- [ ] **Step 1: Format and inspect scope**

Run the repository formatter, then inspect every changed path:

```bash
cd src
make format
cd ..
git status --short
git diff --stat HEAD~3
git diff --check
```

If `make format` changes a source/header not otherwise touched by this plan, preserve concurrent user work and do not stage that unrelated file. Report it instead of restoring or deleting it.

- [ ] **Step 2: Run the complete native build and test suite**

Run sequentially:

```bash
cd src
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 --output-on-failure
```

Expected: the build exits 0 and all discovered tests pass with zero failures.

- [ ] **Step 3: Run final repository checks**

Run:

```bash
cd ..
git diff --check
git status --short --branch
git log -4 --oneline --decorate
```

Inspect the final diff for accidental protocol-output changes, retained views, unrelated edits, generated artifacts, or runtime/world data.

- [ ] **Step 4: Commit formatting only if needed**

If the formatter changed files that this plan already touched after the Task 3 commit, stage each
such path individually and commit:

```bash
git commit -m "style: format string-view output changes"
```

If no formatting diff remains, do not create an empty commit.
