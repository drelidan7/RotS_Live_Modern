# Phase 4 Wave 1: Foundations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the C++20/toolchain base, delete dead code, fix the test-world isolation rot, burn down the accumulated backlog, and prove the `std::format` conversion pattern on leaf modules — the foundations later Phase 4 waves build on.

**Architecture:** Spec `docs/superpowers/specs/2026-07-09-phase-4-wave-1-foundations-design.md`. Order is dependency-driven: the toolchain/standard bump first (everything else compiles against it), then independent low-risk tasks (dead code, test infra, backlog), then the format-pattern task that exercises the new standard, then exit/docs.

**Tech Stack:** C++20 (`std::format`), debian:trixie g++ 14.2 containers (i386 + x64), AppleClang 21, MSVC 2022 `/std:c++20`, GoogleTest, GitHub Actions.

## Global Constraints

- Full battery ×4 gates every task: i386 container (tests-Makefile runner + ctest via `make test` + `scripts/boot-golden.sh verify`), rots64 (`ctest --preset linux-x64` + `boot-golden.sh --service rots64 verify`), macOS native (`ctest --preset macos-arm64` + `boot-golden.sh --native build/macos-arm64/ageland verify`), windows-msvc CI (required job, full ctest). Baselines at branch start: macOS 645/0/71, i386 ctest 645/0/7 + runner 638/7/0, rots64 645/0/73, Windows 641/0/20. Goldens byte-identical always — a golden diff is a STOP, never a recapture. The ONLY sanctioned behavior delta in this wave is Task 4's MSSP narrowing fix (not golden-covered).
- USER CONSTRAINT: no third-party libraries in the game binary (GoogleTest is test-only tooling). `std::format` is the sanctioned formatting target (user C++20 decision 2026-07-09).
- USER CONSTRAINT: the legacy→JSON conversion/salvage path (decoders, converters, sweep/recover modes, frozen `legacy_*_fixture.bin` goldens) stays intact and buildable everywhere; frozen fixtures are 32-bit-layout files — never regenerated on ANY platform. The i386 container remains the legacy-format guard: same `-m32` ABI, only the compiler version changes.
- Never commit lib/ or log/ content; imperative commit subjects ≤72 chars; backup tarball (`tar -czf /Users/drelidan/rots-lib-backup-$(date +%Y%m%d-%H%M%S).tar.gz -C <repo> lib`) before the first lib/-touching boot of each task; `.claude/.no-autoformat` exists.
- Subagent operational rules: never `git clean`/`reset --hard`/`switch`/`stash`; no `rm -rf build` inside containers (bind mount deletes host trees); i386 test binary runs with cwd `/rots/src/tests`; poll CI with blocking foreground `gh run watch`, never background watchers.
- New-code style: Allman braces, braces on every control-flow body, 4-space indent, every class-scoped member commented with its role. Legacy-file edits match surrounding style.

**Scout anchors (verified 2026-07-09):**
- Standards today: `src/Makefile:31` `-std=c++17` in REQ_CXXFLAGS; `src/tests/Makefile:27` `-std=c++1z`; `src/CMakeLists.txt:73,233` `cxx_std_17` (two targets). Containers: `Dockerfile:8` `FROM --platform=linux/386 i386/debian:bullseye` (g++10); `Dockerfile.x64:10` `FROM --platform=linux/amd64 debian:bookworm` (g++12). `debian:trixie` offers g++ 14.2 on BOTH linux/386 and amd64 (verified via apt-cache).
- Dead code: `src/combat_manager.{h,cpp}` referenced only by each other + build files (`src/Makefile`, `src/tests/Makefile` OBJFILES has `combat_manager.o`, CMakeLists deliberately omits it from ageland but check test target); dead OB/PB/DB = `get_real_ob/get_real_parry/get_real_dodge` (weather/room-arg versions) in `src/char_utils_combat.cpp` (rest of file LIVE); `src/comm.cpp:1648` `write_to_descriptor_new` (zero callers); `SUNPROCESSING` only in `src/utils.h`.
- Test-world clones: `src/tests/interpre_account_menu_tests.cpp:372`, `src/tests/db_loader_tests.cpp:413`, `src/tests/spell_pa_tests.cpp:29` (`ensure_test_world_room`), plus `src/tests/damage_test_context.h` `ensure_test_world()`. RAII precedent: `ScopedDescriptorListReset` (`interpre_account_menu_tests.cpp:208`), `ScopedDescriptorList` (`act_wiz_tests.cpp:123`). Shared global: `room_data::BASE_WORLD` / `world[0]`.
- Backlog anchors: `src/fight.cpp:2712-2733` (file-scope `last_time`/`current_time` steady_clock time_points; first call computes delta vs default-constructed epoch); `src/protocol.cpp:2383-2388` `GetMSSP_Uptime` `sprintf(Buffer, "%d", (int)s_Uptime)` — per the MSSP protocol spec UPTIME **is** the boot epoch, so semantics are CORRECT; the defect is the `(int)` narrowing + misleading seed-doc note; `src/zone.cpp:116-140` `fscanf` `%hd` writes — verify each destination field's real type and align format↔type; `src/shapemdl.cpp` (~:150,:164) + `src/obj2html.cpp` `fprintf(stderr, str)`/`printf(str)` non-literal format strings; `src/comm.cpp:1429,1970` `sprintf("...%d...", SocketType)`.
- Leaf-module sprintf census (fan-in check done): `color.cpp` 8, `char_utils.cpp` 3, `utility.cpp` 27 sprintf/strcpy/strcat sites. `object_utils/environment_utils/wait_functions/clock` have ZERO — Task 5's list is exactly color.cpp, char_utils.cpp, utility.cpp (38 sites).
- `upstream/sprintf-replacement` branch (89b8611) exists as prior art — consult diff for pitfalls before Task 5, do not merge.

---

### Task 1: C++20 + trixie/g++14 toolchain bump

**Files:** Modify `Dockerfile` (base → `debian:trixie` with `--platform=linux/386`; keep package set equivalent — g++/make/cmake/libgtest-dev/python3/telnet/procps; drop `libcrypt-dev` ONLY if nothing references it — grep first), `Dockerfile.x64` (base → `debian:trixie`), `src/Makefile:31` (`-std=c++17` → `-std=c++20`), `src/tests/Makefile:27` (`-std=c++1z` → `-std=c++20`), `src/CMakeLists.txt:73,233` (`cxx_std_17` → `cxx_std_20` both targets + update the flag-parity comments), `.github/workflows/ci.yml` (verify each job's toolchain provides C++20+std::format: containers do after rebase; macOS AppleClang 21 does; windows-2022 MSVC does; the linux-x64 job — read the workflow to see whether it runs in the rots64 container or on the runner directly, and if on the runner confirm/select g++ ≥ 13).

**Interfaces produced:** every build path compiles `-std=c++20`; `#include <format>` + `std::format("{}", 42)` works in all four (prove with one permanent trivial test, e.g. `src/tests/cpp20_format_smoke_tests.cpp`: `EXPECT_EQ(std::format("{:>3}", 7), "  7");` wired into both test Makefiles' SRCS and CMake's test list).

Steps:
- [ ] Rebuild both docker images (`docker compose build rots rots64` or per-service); confirm `g++ --version` = 14.x in each; confirm the i386 image still produces `-m32` binaries (file(1) on a test object).
- [ ] Bump the four standard declarations; write the format smoke test FIRST (RED: fails to compile on C++17), then bump (GREEN).
- [ ] Fix C++20-strictness fallout per error (expected small: deprecated implicit capture of `this` via `[=]`, `char8_t`-adjacent, stricter narrowing in aggregates); each fix mechanical, matching surrounding style; NO behavioral rewrites — if a fix looks behavioral, STOP and escalate.
- [ ] Full battery ×4 (rebuild everything from clean trees INSIDE each environment — new compiler invalidates caches; do NOT `rm -rf` via the bind mount, use each build system's clean target or remove only preset subdirs from the HOST side). Goldens byte-identical — this is the task's entire point: prove the bump is behaviorally invisible. Fixture suites + LLP64 probe re-verify struct layouts under g++14 for free.
- [ ] Push; confirm all four required CI jobs green (blocking `gh run watch`).
- [ ] Commit(s): `build: move containers to debian:trixie (g++ 14)` and `build: raise language standard to C++20`.

---

### Task 2: Dead code deleted, not modernized

**Files:** Delete `src/combat_manager.cpp`, `src/combat_manager.h`. Modify `src/char_utils_combat.cpp` (+ its header if the dead trio is declared there) to remove ONLY the dead `get_real_ob`/`get_real_parry`/`get_real_dodge` (the weather/room-arg versions); `src/comm.cpp` remove `write_to_descriptor_new` (:1648 region); `src/utils.h` remove the `SUNPROCESSING` block; `src/Makefile` + `src/tests/Makefile` (drop `combat_manager.o` from OBJFILES) + `src/CMakeLists.txt` (confirm/remove any combat_manager reference in the test target); CLAUDE.md + AGENTS.md "Dead / Unused Code" sections rewritten: combat_manager DELETED in Phase 4 Wave 1, live OB/PB/DB remain `utility.cpp::get_real_OB/get_real_parry/get_real_dodge`, keep the grep-for-callers heuristic text.

**Steps:**
- [ ] For EACH deletion target, record caller-grep proof in the report BEFORE deleting (`grep -rn '<symbol>(' src/` — only self-references/dead-code references allowed). If ANY live caller appears, STOP for that symbol and report it instead of deleting.
- [ ] Delete; build all paths; full battery ×4 — goldens byte-identical (deleting dead code changes nothing).
- [ ] Update the two doc sections; commit `refactor: delete dead combat_manager and other proven-dead code`.

---

### Task 3: Test-world RAII fixture (fixes monolithic-runner isolation rot)

**Files:** Create `src/tests/test_world.h` (single shared fixture). Modify `src/tests/interpre_account_menu_tests.cpp:372`, `src/tests/db_loader_tests.cpp:413`, `src/tests/spell_pa_tests.cpp:29` (replace each local `ensure_test_world_room` clone), `src/tests/damage_test_context.h` (migrate its `ensure_test_world()` to the shared fixture or delegate to it — smallest change that unifies ownership).

**Interfaces produced (binding shape — implementer may refine internals, not the contract):**
```cpp
// test_world.h — RAII owner of the process-wide test world (room_data::BASE_WORLD/world[]).
// Constructor: allocates a minimal 1-room world if absent, or RESETS room 0 to the
// canonical state if a world already exists (freeing prior strdup'd name/description
// before re-assigning — no leaks, no stale pointers). Always clears world[0].people.
// Destructor: restores what it found (or tears down what it created) so the NEXT
// test/suite starts clean — mirrors ScopedDescriptorListReset's convention.
class ScopedTestWorld
{
public:
    ScopedTestWorld();
    ~ScopedTestWorld();
    ScopedTestWorld(const ScopedTestWorld&) = delete;
    ScopedTestWorld& operator=(const ScopedTestWorld&) = delete;
    room_data& room(); // the canonical test room (world[0])
private:
    // Whether this instance allocated BASE_WORLD (owns teardown) or found one (resets only).
    bool owns_world_;
};
```

**Steps:**
- [ ] Reproduce first (RED evidence): on macOS or rots64, `./ageland_tests --gtest_repeat=2` from the monolithic build — capture the segfault (per the seed doc it lands in `InterpreAccountMenu.SelectingSameLinklessActiveCharacterReconnectsExistingBody` pass 2).
- [ ] Implement `ScopedTestWorld`; replace the three clones + damage_test_context; every replaced test compiles and its per-test behavior is unchanged (ctest counts identical).
- [ ] GREEN evidence: `--gtest_repeat=2` clean AND 10 randomly-chosen `--gtest_shuffle` seeds clean (record the seeds), macOS + rots64 monolithic binaries.
- [ ] Full battery ×4 (ctest counts unchanged; goldens byte-identical). Commit `test: RAII test-world fixture; fix cross-suite state pollution`.

---

### Task 4: Backlog burn-down (small pinned fixes)

**Files:** `src/fight.cpp:2712-2733`, `src/protocol.cpp:2383-2388`, `src/zone.cpp:116-140`, `src/shapemdl.cpp` (~:150,:164 + any sibling), `src/obj2html.cpp` (same class), `src/comm.cpp:1429,1970`; tests in the nearest existing suites (fight_proc_tests.cpp or damage_tests.cpp for the fight fix; protocol_tests.cpp for MSSP).

**The fixes (each = test-first where testable, one commit per logical group):**
- [ ] `perform_violence` first-call: guard the delta computation — on the first call (`last_time == std::chrono::steady_clock::time_point{}`) set `last_time = current_time` before computing, yielding delta 0 instead of epoch garbage. Pin with a test (reset the file-scope state via the TESTING seam if one exists; if the statics aren't reachable from tests, add a `#ifdef TESTING` reset hook matching existing conventions in that file — say which you did).
- [ ] MSSP UPTIME: semantics are CORRECT (boot epoch per MSSP spec) — fix ONLY the `(int)` narrowing (`sprintf(Buffer, "%d", (int)s_Uptime)` → format the full `time_t` via `%lld`/`static_cast<long long>`), add a comment stating UPTIME = boot epoch per the MSSP spec (so the seed-doc confusion dies here), and pin with a protocol test if the value is reachable. Commit message calls out: output widens only when epoch > INT_MAX (2038) — no observable change today.
- [ ] `zone.cpp` fscanf: for each `%hd`/destination pair, print the destination field's declared type; align the format spec to the type (or the type to the on-disk semantic if the field is wrongly declared — do NOT change struct layouts of anything persisted; zone cmd structs are boot-time only, verify that before touching). All world-file parsing quirks (`\n\r`, missing trailing newlines) untouched.
- [ ] Non-literal format strings: `fprintf(stderr, str)` → `fputs(str, stderr)` (or `"%s"`), `printf(str)` likewise, at the shapemdl/obj2html sites + any same-class hit from `grep -rn 'printf([a-z_]*)' src/*.cpp` (record census).
- [ ] `comm.cpp` SocketType prints: cast explicitly (`static_cast<long long>` + `%lld`, or int cast where the value is known-small) — same spelling both platforms.
- [ ] Full battery ×4; goldens byte-identical (none of these paths are golden-covered; verify that claim, don't assume it).

---

### Task 5: `std::format` pattern on leaf modules (color, char_utils, utility)

**Files:** `src/color.cpp` (8 sites), `src/char_utils.cpp` (3), `src/utility.cpp` (27); characterization tests FIRST in `src/tests/color_tests.cpp`, `src/tests/char_utils_tests.cpp`, and a new-or-existing utility test home (check for `utility` coverage in existing suites; create `src/tests/utility_format_tests.cpp` if none).

**Pattern (binding — this is what later waves copy):**
- BEFORE converting each function: a characterization test pinning its current output byte-for-byte for representative + edge inputs (negative numbers, width/padding, empty strings, max-length strings near buffer bounds).
- Conversion: `sprintf(buf, "%-20s %3d", name, n)` → `std::string s = std::format("{:<20} {:>3}", name, n);` — printf→format-spec translation table applied per site (`%s`→`{}`, `%d`→`{}`, `%-Ns`→`{:<N}`, `%Nd`→`{:>N}`, `%0Nd`→`{:0>N}` — CAREFUL: `%5.2f`→`{:5.2f}`, `%x`→`{:x}`; escape literal `{`/`}` as `{{`/`}}`).
- Sinks stay C-string-based where callers require it (`send_to_char(s.c_str(), ch)`); do NOT change public signatures in wave 1 unless the function is file-local.
- `strcpy`/`strcat` chains building a message → single `std::format`/`std::string` composition; bounded copies into PERSISTED or fixed-layout buffers stay `snprintf`-style with an explicit size (never format into a fixed char[] via .copy without terminating).
- Consult `git diff master upstream/sprintf-replacement -- src/color.cpp` (etc.) for prior-art pitfalls before starting; cherry-pick knowledge, not commits.

**Steps:**
- [ ] Per module: characterization tests (RED-proof: run them against unmodified code — they must PASS pre-conversion; that IS the pin) → convert → tests still pass byte-identically → module commit (`refactor: color.cpp output composition on std::format` etc.).
- [ ] Full battery ×4 after each module (color.cpp especially — its output is everywhere in goldens' ANSI content; a golden diff = your conversion changed bytes = STOP and fix, never recapture).

---

### Task 6: Exit — docs, tracking issue, ledger

**Files:** `docs/BUILD.md` (toolchain matrix: trixie/g++14 containers, C++20 all platforms, std::format note), `docs/superpowers/specs/2026-07-06-cpp-modernization-design.md` (dated amendment note in the Dependencies section: fmt superseded by std::format per user C++20 decision + no-third-party constraint; Asio row already superseded by rots_net), CLAUDE.md/AGENTS.md (C++20 mentions where C++17 is stated — grep `c++17\|C++17`).

**Steps:**
- [x] Doc updates; verify zero stale `c++1z`/`C++17` claims remain in docs (source comments referencing history are fine).
- [x] Create the Windows operational-gaps tracking issue via `gh issue create` on drelidan7/RotS_Live_Modern, consolidating: sendmail no-op, umask/ACLs, backtrace, SIGVTALRM watchdog, WSA error-reporting polish (perror-vs-WSAGetLastError, WSAStartup retcode), Windows-boot prerequisites (Windows host + world data + boot-golden.sh port). Quote issue URL in the report.
- [x] Exit battery ×4 + CI run URL recorded; ledger updated with new baselines (test counts will have grown: +1 format smoke, +Task 3/4/5 tests — record exact).
- [x] Commit `docs: record Phase 4 Wave 1 exit`.

---

## Self-review notes (done at write time)

- Spec coverage: Task 1↔spec§Task1, 2↔§2, 3↔§3, 4↔§4 (MSSP resolved: semantics correct, fix narrowing), 5↔§5 (leaf list finalized by census: color/char_utils/utility — the other four candidates have zero sites), 6↔§6. Deferred list matches spec.
- No placeholders; exact anchors verified 2026-07-09; interfaces (ScopedTestWorld, cpp20 smoke test) fully specified.
- Type consistency: SocketType prints (Task 4) consistent with rots_net conventions; ScopedTestWorld mirrors existing Scoped* naming.

---

## Wave 1 exit

**Date:** 2026-07-10. **Final commit:** `1d15155` (`docs: reconcile toolchain/test-count claims for Phase 4 Wave 1`), on `modernization/phase-4-wave-1`, branched from `master` at `3536002`. All six wave tasks (toolchain bump, dead-code deletion, test-world RAII, backlog burn-down, `std::format` leaf-module pattern, exit docs/tracking) complete and reviewed; the tree is docs-only on top of Task 5's `7a01f49`, so this exit re-verified rather than changed runtime behavior.

**Exit battery ×4 (actuals, this session, 2026-07-10):**

| Leg | Command(s) | Result |
|---|---|---|
| i386 container | `docker compose run --rm rots` tests-Makefile runner (`../../bin/tests`) | 664 tests: 657 passed, 0 failed, 7 skipped |
| i386 container | `cmake -S src -B build && cmake --build build --target ageland_tests && ctest` | 664/664 passed, 0 failed, 7 skipped |
| i386 container | `scripts/boot-golden.sh verify` | boot log matches golden |
| rots64 container | `cmake --preset linux-x64 && cmake --build --preset linux-x64 && ctest --preset linux-x64` | 664/664 passed, 0 failed, 73 skipped |
| rots64 container | `scripts/boot-golden.sh --service rots64 verify` | boot log matches golden |
| macOS native | `build/macos-arm64/ageland_tests` direct invocation (ctest-equivalent — see note) | 664 tests: 593 passed, 0 failed, 71 skipped |
| macOS native | `scripts/boot-golden.sh --native build/macos-arm64/ageland verify` | boot log matches golden |
| CI (windows-msvc + linux-x64 + macos-arm64 + legacy-32bit, all required) | push + blocking `gh run watch` | all four green, run 29082555190 |

**macOS local-run note:** this machine's iCloud FileProvider wedge on `~/Documents` (first surfaced in Wave 1 Task 3, `.superpowers/sdd/w1-task-3-report.md`) is still present — confirmed again this session (`cmake --preset macos-arm64` and a bare `os.listdir('/Users/drelidan/Documents')` both hung indefinitely and had to be killed). Since this task made no source changes, the pre-existing `build/macos-arm64/ageland`/`ageland_tests` binaries (built earlier the same day, before this task started) were still current; the ctest leg was therefore run by invoking `ageland_tests` directly from the repo root (bypassing cmake/ctest entirely, so the FileProvider wedge never triggers) — identical partition to the macOS CI leg's 664/0/71. `boot-golden.sh --native` also bypasses cmake/docker by design, so it ran normally. This is an environment issue local to this Mac, not a code or CI issue — CI's macOS job (which runs on GitHub's runners, unaffected) is the leg of record for the required gate.

**Test-count baselines (final, Wave 1 exit):** 664 total on i386/rots64/macOS (664/0, skips 71 macOS / 7 i386 / 73 rots64); Windows 660/0/20 (verified from the exact Windows ctest summary line of CI run 29080797408, Task 5's green run: `660/660` tests, `100% tests passed, 0 tests failed out of 660`, 20 skipped). Both CLAUDE.md and AGENTS.md have been updated to state these numbers (previously stale at "~645 (641 Windows)").

**Tracking issues opened (this task):**
- Windows operational gaps: https://github.com/drelidan7/RotS_Live_Modern/issues/1 (sendmail no-op, umask/ACLs, backtrace, SIGVTALRM watchdog, WSA error-reporting polish, Windows-boot prerequisites)
- Phase 4 backlog (parked fixes and follow-ups): https://github.com/drelidan7/RotS_Live_Modern/issues/2 (zone.cpp `-1`/`65535` sentinel fix — parked patch + golden-recapture sign-off needed; world-data format-template hardening; mage_tests 5th test-world clone; DamageTranscriptSeed42 corpse-leak test hygiene; object_utils.cpp `get_weapon_damage` orphaned twin; stale `src/tags` ctags index)

**CI run of record:** https://github.com/drelidan7/RotS_Live_Modern/actions/runs/29082555190 — all four required jobs (`Linux i386 legacy`, `Linux x64`, `macOS arm64`, `Windows MSVC`) green.

**Deferred list — confirmed still deferred, matches spec §"Explicitly deferred from wave 1":**
- Y2038-class `int`-timestamp narrowings (needs a deliberate on-disk format version bump).
- RAII / owner-explicit smart-pointer wave (needs the `char_data`/`obj_data` lifecycle audit first).
- Intrusive-list → STL container migrations; `char[MAX_STRING_LENGTH]` → `std::string`; `enum class` / namespace introduction; god-file splits.
- C++23 bump (MSVC stabilization gate).
- Remaining `utility.cpp` sprintf/strcpy/strcat sites not converted in Task 5 (7 of the 27 census sites — legitimately justified skips per the Task 5 report, e.g. proto-fixture gaps).
- The broader `act_*.cpp`/`fight.cpp`/`db.cpp`/`comm.cpp` sprintf/strcpy/strcat population — Wave 1 proved the `std::format` conversion pattern on leaf modules only (`color.cpp`, `char_utils.cpp`, `utility.cpp`); the high-fan-in modules are explicitly later-wave work per the parent spec's "leaf utilities first; fight.cpp, db.cpp, comm.cpp last" ordering.

Wave 1 is complete. Recommend the next session pick up Phase 4's next wave (or address a backlog item from issue #2) per the parent spec's phase ordering.
