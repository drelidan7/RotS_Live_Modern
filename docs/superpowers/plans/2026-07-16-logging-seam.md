# Platform Logging Seam (Dependency-Inverted Log Sink) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `rots_platform` a logging facility that owns the raw timestamped stderr write and a dependency-inverted sink interface; make `log`/`mudlog` thin wrappers, move `vmudlog` to L0, and let `safe_template.cpp` rejoin `rots_platform` — proven by the existing whole-archive link check. Spec: docs/superpowers/specs/2026-07-16-library-architecture-design.md §13 (last bullet).

**Architecture:** `rots/platform/log.h` (first file of the platform include tree) declares `rots::log::{Sink, set_sink, write_stderr, write}` plus the mudlog type constants and a `static_assert`-guarded broadcast level; `src/rots_log.cpp` (flat, like every platform TU) implements them and hosts `vmudlog` verbatim. The game-coupled half of `mudlog` — the `LEVEL_AREAGOD` clamp, `PRF_LOG*` preference gating, `descriptor_list` walk, color framing — moves verbatim into a sink lambda in `comm.cpp`, registered once in `run_the_game`. Static dependencies point down; control flows up through the callback at runtime.

**Tech Stack:** C++20, existing CMake presets + flat Makefiles, GoogleTest, `PlatformLayerAcyclicity` whole-archive link check.

## Global Constraints

- **Behavior-sensitive, zero observable change:** `mudlog` is on nearly every logging path. The moved gating must be **verbatim**: `file` flag gates the `"%d, %-19.19s :: "` stderr line; `level < 0` returns after the file write (no broadcast); the sink applies `level < LEVEL_AREAGOD → LEVEL_AREAGOD`, wraps via `std::format("[ {} ]\n\r", …)`, skips `connected`/`PLR_WRITING`, computes the 3-bit `PRF_LOG1/2/3` preference, gates on `GET_LEVEL >= level && log_preference >= type`, frames with `CC_FIX(ch, CGRN)`/`CC_NORM(ch)` via `send_to_char`. Goldens (`CharacterizationCombatTest.*`, `CharacterizationJson.*`, boot goldens) stay byte-for-byte green.
- **Current code locations** (branch `arch/logging-seam` @ 3280c73): `log()` utility.cpp:1289, `mudlog()` utility.cpp:1318, `vmudlog()` utility.cpp:1383; prototypes utils.h:99-108; type constants `OFF/BRF/NRM/SPL/CMP` utils.h:181-187; `descriptor_list` defined comm.cpp:106; `run_the_game` comm.cpp:568 (sets `descriptor_list = NULL` at :574, first `log()` call at :576).
- **No sink registered ⇒ notify is a no-op.** Byte-identical because today's broadcast over the pre-boot empty `descriptor_list` also emits nothing; `rots_convert` (future) will simply never register a sink.
- **`src/` never joins an include path** (limits.h shadow). The new include root is `platform/include` (contains only `rots/`), wired exactly like `core/include`/`persist/include` in all four build systems.
- **Sources stay flat** (`src/rots_log.cpp`, not `platform/log.cpp`) — the flat Makefiles compile same-dir only; only headers live in the pathed tree (header-split precedent).
- **Verification cadence per task:** macOS `macos-arm64` build + ctest + native boot golden AND rots64 build + ctest + boot golden. Docker gates run synchronously; if the harness auto-backgrounds one, WAIT for its completion notification. New/rewritten test files additionally need a `macos-arm64-asan` run. i386 battery at branch finalization only.
- **CRLF/format-hook:** edit existing files via binary-mode python (established practice); new files may use Write.
- Warnings are errors on all targets. Current test count: **1248**.

---

## File Structure

```
src/
  platform/include/rots/platform/log.h   # NEW — Sink, set_sink, write_stderr, write, type consts, vmudlog decl
  rots_log.cpp                            # NEW — implementations + vmudlog (flat, joins ROTS_PLATFORM_SOURCES)
  utility.cpp                             # log()/mudlog() become wrappers; vmudlog + its statics removed
  utils.h                                 # includes rots/platform/log.h; drops moved constants + vmudlog decl
  comm.cpp                                # register_mudlog_broadcast_sink() + registration in run_the_game
  comm.h                                  # declares register_mudlog_broadcast_sink()
  CMakeLists.txt                          # platform include dir; rots_log.cpp + safe_template.cpp into ROTS_PLATFORM_SOURCES
  Makefile                                # OBJFILES += rots_log.o; -Iplatform/include
  tests/Makefile                          # -I../platform/include; rots_log.o rule (existing per-object pattern)
  tests/platform_log_tests.cpp            # NEW — capturing-sink tests
docs/BUILD.md                             # platform library section update
```

---

## Task 1: Platform log facility (additive — nothing rewired yet)

**Files:**
- Create: `src/platform/include/rots/platform/log.h`, `src/rots_log.cpp`
- Modify: `src/CMakeLists.txt` (ROTS_PLATFORM_SOURCES + include dir), `src/Makefile` (OBJFILES + `-Iplatform/include` in ALL_CPPFLAGS), `src/tests/Makefile` (`-I../platform/include` in CXXFLAGS; add a `rots_log.o` per-object rule copying the existing pattern, e.g. the `clock.o` rule)

**Interfaces:**
- Produces: `rots::log::Sink` = `std::function<void(std::string_view msg, char type, int level)>`; `Sink set_sink(Sink sink)` (returns the previous sink — boot registers once, tests save/restore); `void write_stderr(std::string_view message)` (byte-identical to today's `log()` body); `void write(std::string_view message, char type, int level, bool to_file)` (mudlog's platform half: typed stderr line when `to_file`, return when `level < 0`, else notify the sink if one is set); `void vmudlog(char type, const char* format, ...)` (global namespace, verbatim vsnprintf body, now calling `write(buf, type, kVmudlogBroadcastLevel, true)`); `#define OFF/BRF/NRM/SPL/CMP` moved verbatim; `inline constexpr int kVmudlogBroadcastLevel = <value of LEVEL_GOD — read it from rots/core/types.h and hard-code the literal>` with a comment that utility.cpp static_asserts it against `LEVEL_GOD`.
- In this task the new facility is **built but unused** — utils.h/utility.cpp untouched, so `vmudlog` would be defined twice if both existed: therefore `rots_log.cpp` in THIS task defines everything EXCEPT `vmudlog` (its decl in log.h is `#ifdef`-free but the definition lands in Task 2 when utility.cpp's copy is deleted — to keep every intermediate commit linking, gate nothing; simply do not define `vmudlog` in rots_log.cpp yet and note it in a comment).

- [ ] **Step 1:** Create `log.h` with `#pragma once`, `<functional>`, `<string_view>`, the `rots::log` namespace API above, the moved-verbatim `OFF/BRF/NRM/SPL/CMP` block (with its `/* defines for mudlog() */` comment; do NOT delete them from utils.h yet — duplicate identical `#define`s are benign for one task), and the `vmudlog` prototype with a note that the definition arrives with the Task 2 rewire.
- [ ] **Step 2:** Create `src/rots_log.cpp`: a file-local `Sink` (function-local static in an accessor, avoiding init-order issues), `set_sink` (swap + return old), `write_stderr` (copy `log()`'s body verbatim including the `rots::text::truncate_at_null` boundary call and the LLP64 `time_t` comment — include `"text_view.h"` via the same relative spelling other platform TUs use, i.e. bare `"text_view.h"` since the file is flat in src/), `write` (mudlog's file-branch verbatim + `level < 0` return + sink notify).
- [ ] **Step 3:** Wire the build systems: append `rots_log.cpp` to `ROTS_PLATFORM_SOURCES`; `target_include_directories(rots_platform PUBLIC platform/include)`; add `platform/include` to `ageland_tests`' include dirs (it does not link the lib); `src/Makefile`: `rots_log.o` into OBJFILES and `-Iplatform/include` into ALL_CPPFLAGS; `src/tests/Makefile`: `-I../platform/include` into CXXFLAGS and a `rots_log.o` rule per the existing per-object pattern.
- [ ] **Step 4:** Gates: macOS build + ctest (1248) + boot golden; rots64 build + ctest + boot golden.
- [ ] **Step 5:** Commit: `feat: rots/platform log facility (sink seam, unused)`.

## Task 2: Rewire — wrappers, sink registration, vmudlog to L0

**Files:**
- Modify: `src/utility.cpp`, `src/utils.h`, `src/comm.cpp`, `src/comm.h`, `src/rots_log.cpp`

**Interfaces:**
- Consumes: everything Task 1 produced.
- Produces: `void register_mudlog_broadcast_sink()` (comm.cpp, declared in comm.h) — installs the verbatim broadcast lambda; called in `run_the_game` immediately after `descriptor_list = NULL;` (comm.cpp:574) and before the first `log()`.

- [ ] **Step 1:** `src/rots_log.cpp`: add the `vmudlog` definition (verbatim BUFSIZE-2048 vsnprintf body; final line becomes `rots::log::write(buf, type, rots::log::kVmudlogBroadcastLevel, true);`).
- [ ] **Step 2:** `src/utility.cpp`: delete `vmudlog` (1383-1395); reduce `log()` to `rots::log::write_stderr(message);` and `mudlog()` to `rots::log::write(message_body, type, level, file != 0);` (keep both game-facing signatures/doc comments); add `#include "rots/platform/log.h"` and `static_assert(rots::log::kVmudlogBroadcastLevel == LEVEL_GOD, "platform vmudlog level diverged from LEVEL_GOD");`. `log_death_trap`, `mudlog_debug_mob`, `mudlog_aliased_mob` stay untouched (they call the wrappers).
- [ ] **Step 3:** `src/utils.h`: replace the `OFF/BRF/NRM/SPL/CMP` block and the `vmudlog` prototype with `#include "rots/platform/log.h"` (placed with the other pathed includes); keep `log`/`mudlog`/`mudlog_*`/`log_death_trap` prototypes as-is.
- [ ] **Step 4:** `src/comm.cpp` + `comm.h`: add `register_mudlog_broadcast_sink()` containing today's broadcast half **verbatim** (clamp, `std::format` wrap, loop, preference math, color framing — lifted from the old mudlog body) as a lambda passed to `rots::log::set_sink`; call it in `run_the_game` right after `descriptor_list = NULL;`.
- [ ] **Step 5:** Full gates (macOS + rots64, ctest + boot goldens). **Boot goldens are the key behavior check here** — the boot log exercises `log()` heavily and `mudlog` paths run under it. If `utility_format_tests` assertions fail because they relied on in-process broadcast behavior with no sink registered, register the production sink in those fixtures (call `register_mudlog_broadcast_sink()` in the test setup next to the existing `ScopedDescriptorListReset`) — list any such edit in the report.
- [ ] **Step 6:** Commit: `refactor: log/mudlog over the platform sink seam; vmudlog moves to L0`.

## Task 3: safe_template rejoins rots_platform

**Files:**
- Modify: `src/CMakeLists.txt` (move `safe_template.cpp` from ROTS_SERVER_SOURCES to ROTS_PLATFORM_SOURCES; rewrite the exclusion comment at ~lines 69-73 into a "rejoined via the logging seam" note)

- [ ] **Step 1:** Move the source-list entry; update the comment to record: vmudlog now resolves inside `rots_platform` (rots_log.cpp), so the archive is self-contained again; note the known include-purity debt (safe_template.cpp still textually includes utils.h for `nz`; the LINK graph — the enforced invariant — is clean).
- [ ] **Step 2:** Build and run `ctest --preset macos-arm64 -R PlatformLayerAcyclicity -V` — the whole-archive link check must PASS with safe_template + rots_log in the archive. This is the task's acceptance test and the spec §13 payoff.
- [ ] **Step 3:** Full gates both hosts. Commit: `build: safe_template.cpp rejoins rots_platform (logging seam cut its upward edge)`.

## Task 4: Capturing-sink tests

**Files:**
- Create: `src/tests/platform_log_tests.cpp`; Modify: `src/CMakeLists.txt` (ROTS_TEST_SOURCES), `src/tests/Makefile` (test list, following an existing test file's wiring)

- [ ] **Step 1:** Write tests using an RAII guard that `set_sink`s a capturing lambda and restores the previous sink in the destructor: (a) `mudlog(msg, BRF, LEVEL_GOD, FALSE)` notifies the sink exactly once with the raw body, `type == BRF`, `level == LEVEL_GOD`; (b) `level = -1` produces NO notification (file-only path); (c) `vmudlog(BRF, "fmt %d", 7)` reaches the sink with the formatted text and `level == LEVEL_GOD` (proving the static_asserted constant end-to-end); (d) `set_sink` returns the previous sink (chain restore works); (e) `write_stderr` never notifies the sink.
- [ ] **Step 2:** Run the new tests (`ctest -R PlatformLog`), then the full suite — expect **1253** (1248 + 5) or adjust to the actual added count; update expected totals in the report.
- [ ] **Step 3:** New-test sanitizer gate: `macos-arm64-asan` configure + build + ctest — clean.
- [ ] **Step 4:** Full gates both hosts. Commit: `test: capturing-sink coverage for the platform log seam`.

## Task 5: Docs + finalization

- [ ] **Step 1:** `docs/BUILD.md`: extend the library-layering section — `rots_platform` membership now 9 TUs (7 + rots_log + safe_template), the sink seam in one paragraph, the new `platform/include` root. Spec §13: mark the logging-seam bullet as implemented (as-built note: single stderr target; clamp/preferences live in the app sink; `kVmudlogBroadcastLevel` static_assert).
- [ ] **Step 2:** Branch finalization battery (scripted as in prior waves): macOS ASan (already run in Task 4 — skip if unchanged since), i386 `make test` + monolithic runner + boot golden, flat i386 `make all`, `linux-x86-legacy` preset build + archive check. All green.
- [ ] **Step 3:** Commit docs; whole-branch review; then merge/push per owner's call.

---

## Self-Review Notes

- **Spec §13 coverage:** sink interface ✓ (Task 1), app-layer verbatim broadcast sink ✓ (Task 2), thin wrappers ✓ (Task 2), vmudlog in platform ✓ (Task 2), safe_template rejoins L0 with the link check as proof ✓ (Task 3), capturing-sink test bonus ✓ (Task 4). Deviation from the spec sketch, justified by the fact-check: no separate `writef` in the namespace (global `vmudlog` IS the printf-style entry, preserved for its 76 callers); the clamp moved into the sink because `LEVEL_AREAGOD` is a game constant; `kVmudlogBroadcastLevel` + static_assert bridges `LEVEL_GOD` to L0 without a header-layer violation.
- **Intermediate states all build:** Task 1 is purely additive (no duplicate vmudlog — definition deferred); Task 2 swaps definition sites atomically in one commit.
- **Type-consistency:** `Sink`, `set_sink`, `write_stderr`, `write`, `kVmudlogBroadcastLevel`, `register_mudlog_broadcast_sink` used identically across tasks.
